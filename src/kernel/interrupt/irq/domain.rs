use core::any::Any;

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
/// 再次自动均衡时，调用者应重新传入 Auto
#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub enum Affinity {
    #[default]
    Auto,
    Cpu(CpuId),
}

/// 生命周期仅在可等待的管理上下文调用
pub trait Domain: Send + Sync {
    /// 构造 inactive 数据；失败时由局部对象的 Drop 回滚已取得的资源
    fn allocate(&self, irq: IrqNumber, arg: &dyn Any) -> Result<IrqData, IrqError>;

    fn free(&self, data: &IrqData);

    /// 成功后路由可用于 chip 回调，但中断源必须保持屏蔽
    /// 失败时本层自行撤销局部改变；core 回滚已成功的父层
    fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &mut Affinity,
    ) -> Result<(), IrqError>;

    /// 源已屏蔽，等待本层已发起的事件完成；不得持有运行时回调所需的锁
    fn synchronize(&self, data: &IrqData);

    /// 只释放路由；调用前已完成 synchronize，或者激活从未开放过投递
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

pub(super) fn synchronize(data: &IrqData) {
    data.domain().synchronize(data);
    if let Some(parent) = data.parent() {
        synchronize(parent);
    }
}
