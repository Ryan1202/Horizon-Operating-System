use core::any::Any;

use super::IrqData;

/// Core 只调用最外层 chip；parent 操作由 chip 根据硬件语义显式转发
pub trait IrqChip: Any + Send + Sync {
    /// 屏蔽中断
    fn mask(&self, data: &IrqData);
    /// 取消屏蔽中断
    fn unmask(&self, data: &IrqData);
    /// 确认接收到中断
    fn ack(&self, data: &IrqData);
    /// 中断处理已完成
    fn eoi(&self, data: &IrqData);
    /// 屏蔽并确认接收到中断
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
