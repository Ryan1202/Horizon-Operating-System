use core::{arch::asm, ffi::c_int};

use crate::{
    arch::x86::{
        drivers::interrupt::apic::LocalXApic,
        kernel::interrupt::{apic::LocalApic, vector::VECTOR_MANAGER},
    },
    kernel::interrupt::{self, Interrupt, InterruptGuard},
};

pub mod apic;
pub mod legacy;
mod probe;
pub mod vector;

static VECTOR_PROBE: probe::Probe = probe::Probe::new();

fn synchronize_vector(route: vector::VectorRoute) {
    use core::hint::spin_loop;

    loop {
        let local = LocalXApic::with_current(|lapic| {
            (lapic.id().get() == route.apic_id.get()).then(|| lapic.vector_busy(route.vector))
        })
        .expect("synchronize before LAPIC initialization");

        if let Some(busy) = local {
            if !busy {
                return;
            }

            // with_current 已恢复本地中断，给 IRR 中的事件执行机会。
            spin_loop();
            continue;
        }

        while !VECTOR_PROBE.start(route.apic_id.get() as u8, route.vector) {
            spin_loop();
        }

        LocalXApic::with_current(|lapic| lapic.send_sync_ipi(route.apic_id))
            .expect("IPI before LAPIC initialization");

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

/// 新 IDT vector 入口；不改变旧 ISA 入口及其 C 驱动的所有权
#[unsafe(no_mangle)]
extern "C" fn vector_dispatch(vector: u8) {
    let irq = LocalXApic::with_current(|lapic| match vector {
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
    })
    .expect("vector entry before LAPIC initialization");

    if let Some(irq) = irq {
        interrupt::handle_mapped(irq);
    }
}

const IRQ_COUNT: u8 = 16;

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

#[unsafe(no_mangle)]
extern "C" fn irq_dispatch(irq: c_int) {
    assert!(
        (0..IRQ_COUNT as i32).contains(&irq),
        "invalid x86 IRQ number"
    );
    interrupt::handle(irq as u8);
}

#[unsafe(export_name = "irq_early_init")]
extern "C" fn early_init() {
    VECTOR_MANAGER
        .init()
        .expect("failed to initialize x86 vectors");
    legacy::init_irqs().expect("failed to publish ISA IRQ placeholders");
}
