use core::any::Any;

use crate::kernel::topology::CpuMask;

use super::{IrqData, IrqError, IrqNumber};

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

/// 生命周期仅在可等待的管理上下文调用
pub trait Domain: Send + Sync {
    /// 构造当前 domain 的私有数据
    fn allocate(&self, irq: IrqNumber, arg: &dyn Any) -> Result<IrqData, IrqError>;

    /// 激活 IRQ，返回实际生效的 CPU 集合
    ///
    /// # Safety
    ///
    /// 不允许在已激活的 IRQ 上调用 activate
    unsafe fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &CpuMask,
    ) -> Result<CpuMask, IrqError>;

    /// 释放路由
    ///
    /// # Safety
    ///
    /// 调用前需确保已关闭并屏蔽该 IRQ，且所有旧的执行者已退出
    unsafe fn deactivate(&self, data: &IrqData) -> Result<(), IrqError>;

    /// 源已屏蔽，等待本层已发起的事件完成
    fn synchronize(&self, data: &IrqData);

    /// 更新亲和性，返回实际生效的 CPU 集合
    ///
    /// # Safety
    ///
    /// 只能在已激活的 IRQ 上调用 update_affinity
    unsafe fn update_affinity(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &CpuMask,
    ) -> Result<CpuMask, IrqError>;

    /// 回收旧的路由
    fn try_reclaim_route(&self, data: &IrqData) -> Result<(), IrqError>;
}

/// 激活 IRQ，返回实际生效的 CPU 集合
///
/// # Safety
///
/// 不允许在已激活的 IRQ 上调用 activate
pub(super) unsafe fn activate(
    irq: IrqNumber,
    data: &IrqData,
    affinity: &CpuMask,
) -> Result<CpuMask, IrqError> {
    // SAFETY: 由调用者保证 irq 未激活
    unsafe { data.domain().activate(irq, data, affinity) }
}

/// 释放路由
///
/// # Safety
///
/// 调用前需确保已关闭并屏蔽该 IRQ，且所有旧的执行者已退出
pub(super) unsafe fn deactivate(data: &IrqData) -> Result<(), IrqError> {
    // SAFETY: 由调用者保证
    unsafe { data.domain().deactivate(data) }
}

pub(super) fn synchronize(data: &IrqData) {
    data.domain().synchronize(data);
}
