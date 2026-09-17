//! 架构无关的 IRQ core

mod action;
mod allocator;
mod chip;
mod data;
mod descriptor;
mod domain;
mod flow;
mod handle;
mod number;
mod placeholder;
mod table;

pub(crate) use crate::kernel::thread::scheduler::assert_can_manage_irq as assert_management;
pub use action::{IrqHandler, IrqSharing};
pub use allocator::IrqReservation;
pub use chip::IrqChip;
pub use data::IrqData;
pub use descriptor::IrqDescriptor;
pub use domain::{Affinity, Domain, Polarity, TriggerMode};
pub use flow::Flow;
pub use handle::{IrqHandle, request_irq};
pub use number::{HardwareIrq, INVALID_IRQ, IrqNumber, RawIrq};
pub use table::IRQ_DESCRIPTORS;

use crate::kernel::{
    interrupt::{HardIrqGuard, run_softirq},
    memory::MemoryError,
    thread::{PreemptGuard, scheduler::scheduler},
};

/// 注册或 mapping 构造失败。注销路径无可恢复错误
#[derive(Debug, Clone)]
pub enum IrqError {
    AlreadyPublished,
    InvalidArgument,
    InvalidIrqNumber(usize),
    InvalidHardwareIrq(RawIrq),
    ExclusiveViolation,
    NotFound,
    Busy,
    OutOfMemory(MemoryError),
    OutOfIrq,
    Exhausted,
    Unsupported,
}

impl From<MemoryError> for IrqError {
    fn from(err: MemoryError) -> Self {
        IrqError::OutOfMemory(err)
    }
}

/// `Active` 执行 handler;  `Disabled` / `Stopping` 只完成迟到事件的硬件收尾
pub fn handle_irq(irq: Option<IrqNumber>) {
    let hardirq = HardIrqGuard::new();
    if let Some(irq) = irq {
        let descriptor = IRQ_DESCRIPTORS.lookup(irq);
        if let Some(descriptor) = descriptor {
            if descriptor.is_configured() {
                flow::dispatch(descriptor.as_ref());
            }
        } else {
            printk!("IRQ {} not found in IRQ_DESCRIPTORS", irq.get());
        }
    }

    if let Some(softirq) = hardirq.into_softirq() {
        let point = run_softirq(softirq);
        let guard = PreemptGuard::new();
        if scheduler(&guard).is_initialized() {
            point.try_preempt(guard);
        }
    }
}
