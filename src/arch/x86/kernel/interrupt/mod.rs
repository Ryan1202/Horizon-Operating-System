use core::{arch::asm, cell::SyncUnsafeCell, ffi::c_int, mem::MaybeUninit};

use alloc::sync::Arc;

use crate::{
    arch::x86::kernel::interrupt::{
        legacy::{LEGACY_IRQ_CHIP, LEGACY_IRQ_DOMAIN, LegacyIrqChip},
        vector::VECTOR_MANAGER,
    },
    kernel::{
        interrupt::{
            self, Interrupt, InterruptGuard,
            irq::{Flow, IRQ_DESCRIPTORS, IrqDescriptor, IrqNumber, IrqReservation, IrqSharing},
        },
        memory::kmalloc::Kmalloc,
    },
    printk,
};

pub mod apic;
pub mod legacy;
pub mod vector;

static IRQ_RESERVATION: SyncUnsafeCell<MaybeUninit<IrqReservation>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

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
    VECTOR_MANAGER.init().expect("failed to initialize x86 vectors");
    unsafe { &mut *LEGACY_IRQ_CHIP.get() }.write(Arc::new_in(LegacyIrqChip, Kmalloc::default()));

    let irqs = IrqReservation::reserve((0..IRQ_COUNT as usize).into())
        .expect("failed to reserve x86 IRQs");

    for (irq, irq_number) in (0..IRQ_COUNT).zip(irqs.iter()) {
        let descriptor = IrqDescriptor::new(
            irq_number,
            IrqSharing::Exclusive,
            &LEGACY_IRQ_DOMAIN,
            Flow::Level,
            &(),
        );
        let descriptor = match descriptor {
            Ok(descriptor) => descriptor,
            Err(e) => {
                printk!("failed to create descriptor for IRQ {}: {:?}\n", irq, e);
                break;
            }
        };

        IRQ_DESCRIPTORS.publish(descriptor);
    }
    unsafe { IRQ_RESERVATION.get().write(MaybeUninit::new(irqs)) };
}

pub(crate) fn isa_irq(irq: u8) -> Option<IrqNumber> {
    unsafe { (*IRQ_RESERVATION.get()).assume_init_ref().get(irq as usize) }
}
