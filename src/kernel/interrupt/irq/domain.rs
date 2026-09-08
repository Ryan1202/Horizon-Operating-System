use core::{any::Any, mem::MaybeUninit};

use super::{IrqData, IrqError, IrqNumber};
use crate::kernel::topology::CpuId;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Flow {
    Edge,
    Level,
    FastEoi,
    EdgeEoi,
    Simple,
    PerCpu,
    Bad,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TriggerMode {
    Edge,
    Level,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Polarity {
    High,
    Low,
}

/// 激活时选择投递 CPU；父 domain 将 Auto 写回为实际选中的 Cpu。
/// 再次自动均衡时，调用者应重新传入 Auto。
#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub enum Affinity {
    #[default]
    Auto,
    Cpu(CpuId),
}

/// 生命周期仅在可等待的管理上下文调用
pub trait Domain: Send + Sync {
    fn allocate(
        &self,
        irq: IrqNumber,
        data: &mut MaybeUninit<IrqData>,
        arg: &dyn Any,
    ) -> Result<(), IrqError>;

    fn free(&self, data: &IrqData);

    fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &mut Affinity,
    ) -> Result<(), IrqError>;

    fn deactivate(&self, data: &IrqData);
}

pub(super) fn activate(
    irq: IrqNumber,
    data: &IrqData,
    affinity: &mut Affinity,
) -> Result<(), IrqError> {
    if let Some(parent) = data.parent() {
        activate(irq, parent, affinity)?;
    }
    if let Err(error) = data.domain().activate(irq, data, affinity) {
        if let Some(parent) = data.parent() {
            deactivate(parent);
        }
        return Err(error);
    }
    Ok(())
}

pub(super) fn deactivate(data: &IrqData) {
    data.domain().deactivate(data);
    if let Some(parent) = data.parent() {
        deactivate(parent);
    }
}

pub(super) fn disconnect(data: &IrqData) {
    // data.domain().disconnect(data);
    if let Some(parent) = data.parent() {
        disconnect(parent);
    }
}
