use super::{IrqData, IrqNumber};

/// Runtime 回调必须 hardirq-safe、无失败、不分配、不等待，不能取得管理锁。
/// Core 只调用最外层 chip；parent 操作由 chip 根据硬件语义显式转发。
pub trait IrqChip: Send + Sync {
    fn mask(&self, irq: IrqNumber, data: &IrqData);

    fn unmask(&self, irq: IrqNumber, data: &IrqData);

    fn ack(&self, irq: IrqNumber, data: &IrqData);

    fn eoi(&self, irq: IrqNumber, data: &IrqData);

    fn mask_ack(&self, irq: IrqNumber, data: &IrqData) {
        self.mask(irq, data);
        self.ack(irq, data);
    }
}
