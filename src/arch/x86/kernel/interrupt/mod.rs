use core::arch::asm;

use crate::{
    arch::x86::{
        drivers::interrupt::apic::{IoApics, LocalApic},
        kernel::{acpi::X86Topology, interrupt::vector::VECTOR_MANAGER},
    },
    kernel::{
        interrupt::{self, Interrupt, InterruptGuard, irq::handle_irq},
        thread::PreemptGuard,
        topology::CpuId,
    },
};

pub mod apic;
pub mod legacy;
mod probe;
pub mod vector;

static VECTOR_PROBE: probe::Probe = probe::Probe::new();

fn synchronize_vector(route: vector::VectorRoute) {
    use core::hint::spin_loop;
    let lapic = LocalApic::get();

    loop {
        {
            let _guard = PreemptGuard::new();
            (lapic.id().get() == route.apic_id.get()).then(|| lapic.vector_busy(route.vector));
        }

        while !VECTOR_PROBE.start(route.apic_id.get() as u8, route.vector) {
            spin_loop();
        }

        {
            let _guard = PreemptGuard::new();
            lapic.send_sync_ipi(route.apic_id);
        }

        let busy = loop {
            if let Some(busy) = VECTOR_PROBE.result() {
                break busy;
            }
            spin_loop();
        };

        VECTOR_PROBE.release();

        if !busy {
            return;
        }

        spin_loop();
    }
}

/// 所有 Rust vector 在 hardirq 上下文内分发，释放 LAPIC guard 后统一收尾
#[unsafe(no_mangle)]
extern "C" fn vector_dispatch(vector: u8) {
    interrupt::handle_arch(|| dispatch_vector(vector));
}

fn dispatch_vector(vector: u8) {
    let irq = {
        let lapic = LocalApic::get();
        match vector {
            vector::SYNC_VECTOR => {
                let request = VECTOR_PROBE.requested(lapic.id().get() as u8);
                let busy = request.map(|vector| lapic.vector_busy(vector));

                lapic.eoi();

                if let Some(busy) = busy {
                    VECTOR_PROBE.complete(busy);
                }
                None
            }
            vector::ERROR_VECTOR => {
                lapic
                    .handle_error()
                    .expect("LAPIC error handler unavailable");
                None
            }
            vector::SPURIOUS_VECTOR => {
                lapic.handle_spurious();
                None
            }
            _ => {
                let irq = VECTOR_MANAGER.lookup_apic(lapic.id(), vector);
                if irq.is_none() {
                    lapic.eoi();
                }
                irq
            }
        }
    };

    if let Some(irq) = irq {
        handle_irq(irq);
    }
}

pub struct X86Interrupt;

impl Interrupt for X86Interrupt {
    type Status = usize;

    fn is_enabled() -> bool {
        Self::get_status() & (1 << 9) != 0
    }

    #[inline]
    fn get_status() -> Self::Status {
        let flags: usize;
        // SAFETY: 读取 RFLAGS 寄存器不会破坏程序状态
        unsafe { asm!("pushfq; pop {}", out(reg) flags, options(nomem, preserves_flags)) };
        flags
    }

    #[inline]
    fn enable() {
        // SAFETY: 调用时 IRQ 控制器已经收到 EOI，通用阶段也已经进入
        // softirq 处理阶段
        unsafe { asm!("sti", options(nomem, nostack)) };
    }

    #[inline]
    fn disable() {
        // SAFETY: softirq 收尾需要在关闭中断的状态下完成最终 pending 检查
        // 和 guard 释放，避免遗漏被 hardirq 新增的工作
        unsafe { asm!("cli", options(nomem, nostack)) };
    }

    #[inline]
    fn wait() {
        // SAFETY: sti 后紧接 hlt，CPU 会在下一次可屏蔽中断到来时继续执行，
        // 不会在开启中断与休眠之间留下可丢失唤醒的指令窗口
        unsafe { asm!("sti; hlt", options(nomem, nostack)) };
    }

    #[inline]
    fn save_and_disable<'a>() -> InterruptGuard<'a, Self> {
        let flags;
        unsafe {
            asm!(
                "pushfq",
                "cli",
                "pop {}",
                out(reg) flags,
                options(nomem, preserves_flags)
            )
        }
        InterruptGuard::new(flags)
    }

    #[inline]
    fn restore(status: &Self::Status) {
        // SAFETY: 恢复中断状态不会破坏程序状态
        unsafe { asm!("push {}; popfq", in(reg) *status, options(nomem)) };
    }
}

#[unsafe(export_name = "irq_early_init")]
extern "C" fn early_init() {
    VECTOR_MANAGER
        .init()
        .expect("failed to initialize x86 vectors");
    legacy::init_irqs().expect("failed to publish ISA IRQ placeholders");
}

/// BSP 控制器初始化
///
/// 在开放本地中断、初始化设备 IRQ 之前调用
#[unsafe(no_mangle)]
extern "C" fn interrupt_init() {
    let _interrupt = X86Interrupt::save_and_disable();

    // 旧 PIC 不参与路由，在 LAPIC 软件启用前屏蔽其全部输入
    unsafe {
        asm!("out dx, al", in("dx") 0x21u16, in("al") 0xffu8, options(nomem, nostack));
        asm!("out dx, al", in("dx") 0xa1u16, in("al") 0xffu8, options(nomem, nostack));
    }

    early_init();
    apic::init_current().expect("failed to initialize BSP LAPIC");
    IoApics::get()
        .init(X86Topology::get().ioapics())
        .expect("failed to initialize IOAPICs");

    // SAFETY: BSP 使用逻辑 CPU0，IDT 已安装；C APIC/PIC 驱动已退出构建。
    // 所有 LVT（包括 timer）与 IOAPIC 输入保持屏蔽，设备由各自 handle 开放。
    unsafe { apic::enable_current(CpuId::new(0)) }.expect("failed to enable BSP LAPIC");
}
