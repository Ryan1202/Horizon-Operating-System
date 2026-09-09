//! 架构无关的 IRQ core
//!
//! action：注册入口与节点发布；handle：同步注销
//! table：virq 到 descriptor 的直接指针数组；descriptor/flow：运行状态与分发

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
mod sync;
mod table;

use core::ptr::NonNull;

pub use action::{IrqHandler, IrqSharing, request_irq};
pub use allocator::IrqReservation;
pub use chip::IrqChip;
pub use data::IrqData;
pub use descriptor::IrqDescriptor;
pub use domain::{Affinity, Domain, Flow, Polarity, TriggerMode};
pub use handle::IrqHandle;
pub use number::{HardwareIrq, IrqNumber, RawIrq};
pub(crate) use sync::assert_management;
pub use table::IRQ_DESCRIPTORS;

use crate::kernel::memory::MemoryError;

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

/// 分发已配置且 Active 的 IRQ；尚未接入真实硬件激活
pub fn handle_irq(irq: IrqNumber) -> Option<()> {
    let descriptor = IRQ_DESCRIPTORS.lookup(irq)?;
    if !descriptor.is_configured() {
        return None;
    }

    let pointer = NonNull::from(&*descriptor);
    drop(descriptor);

    // SAFETY: 表锁内已确认配置完成，之后拓扑不再修改，descriptor 常驻
    flow::dispatch(unsafe { pointer.as_ref() })
}
