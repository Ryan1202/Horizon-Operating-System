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
mod sync;
mod table;

pub use action::{IrqHandler, IrqSharing};
pub use allocator::IrqReservation;
pub use chip::IrqChip;
pub use data::IrqData;
pub use descriptor::IrqDescriptor;
pub use domain::{Affinity, Domain, Flow, Polarity, TriggerMode};
pub use handle::IrqHandle;
pub use number::{HardwareIrq, IrqNumber, RawIrq};
// pub use source::IrqSource;
pub use table::IRQ_DESCRIPTORS;

use crate::kernel::memory::MemoryError;

// #[allow(unused_imports)]
// pub(crate) use mapping::platform_mapping;

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

/// 硬件入口必须先翻译编号；未知/已撤销编号返回 NotHandled
/// EOI 属于 flow，调用者不能再重复 EOI
pub fn handle_irq(irq: IrqNumber) -> Option<()> {
    let Some(r#ref) = IRQ_DESCRIPTORS.lookup(irq) else {
        return None;
    };
    // flow::dispatch(r#ref.descriptor(), true)
    Some(())
}

// 宿主测试直接将本文件作为 Cargo 库入口；生产构建不引入模拟依赖
// #[cfg(test)]
// extern crate alloc;
// #[cfg(test)]
// extern crate std;
// #[cfg(test)]
// use irq_core_test_support::{CPU, LOCKS, kernel, lib};
// #[cfg(test)]
// mod tests;
