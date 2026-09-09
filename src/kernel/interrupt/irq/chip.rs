use core::any::Any;

use super::IrqData;

/// Runtime 回调在 descriptor 状态锁内执行，必须 hardirq-safe、无失败、
/// 不分配、不等待，不能重入 IRQ core 或取得管理锁
///
/// Core 只调用最外层 chip；parent 操作由 chip 根据硬件语义显式转发
pub trait IrqChip: Any + Send + Sync {
    fn mask(&self, data: &IrqData);

    fn unmask(&self, data: &IrqData);

    fn ack(&self, data: &IrqData);

    fn eoi(&self, data: &IrqData);

    fn mask_ack(&self, data: &IrqData) {
        self.mask(data);
        self.ack(data);
    }
}

impl dyn IrqChip {
    /// Domain 可从 IrqData 已保存的 chip 恢复具体控制器类型
    pub fn downcast_ref<T: IrqChip>(&self) -> Option<&T> {
        (self as &dyn Any).downcast_ref()
    }
}
