use alloc::sync::Arc;
use core::{any::Any, hint::spin_loop};

use super::{Domain, Flow, IrqError, IrqNumber, allocator::MAX_IRQS, descriptor::IrqDescriptor};
use crate::{
    kernel::{interrupt::irq::IrqSharing, memory::kmalloc::Kmalloc},
    lib::rust::spinlock::Spinlock,
};

pub static IRQ_DESCRIPTORS: IrqTable = IrqTable::new();

type Descriptors = Spinlock<[Option<Arc<IrqDescriptor, Kmalloc>>; MAX_IRQS]>;
pub struct IrqTable(Descriptors);

impl IrqTable {
    const fn new() -> Self {
        Self(Spinlock::new([const { None }; MAX_IRQS]))
    }

    pub fn publish(&self, descriptor: IrqDescriptor) -> Result<(), IrqError> {
        let irq = descriptor.irq.get();
        let descriptor = Arc::new_in(descriptor, Kmalloc::default());
        let mut table = self.0.lock_irqsave();

        if table[irq].is_some() {
            return Err(IrqError::AlreadyPublished);
        }

        table[irq] = Some(descriptor);

        Ok(())
    }

    pub fn unpublish(&self, irq: IrqNumber) -> Result<Arc<IrqDescriptor, Kmalloc>, IrqError> {
        let descriptor = self.0.lock_irqsave()[irq.get()].take();

        // 保留当前引用持续尝试 get_mut，避免在中断上下文中释放 descriptor
        if let Some(mut descriptor) = descriptor {
            loop {
                if let Some(_) = Arc::get_mut(&mut descriptor) {
                    return Ok(descriptor);
                }

                spin_loop();
            }
        }

        Err(IrqError::NotFound)
    }

    pub fn lookup(&self, irq: IrqNumber) -> Option<Arc<IrqDescriptor, Kmalloc>> {
        self.0.lock_irqsave()[irq.get()].clone()
    }

    pub fn realloc(
        &self,
        irq: IrqNumber,
        sharing: IrqSharing,
        domain: &'static dyn Domain,
        flow: Flow,
        arg: &dyn Any,
    ) -> Result<(), IrqError> {
        // 分配可能等待，构造完成后再取得表锁并提交
        let data = domain.allocate(irq, arg)?;

        // descriptor 始终留在表中。若恰好有一次占位 lookup 持有 Arc，释放表锁等它退出后重试
        // 不制造 vector 已映射而 descriptor 暂时缺失的窗口
        let _ = loop {
            let mut table = self.0.lock_irqsave();
            let descriptor = table[irq.get()].as_mut().ok_or(IrqError::NotFound)?;

            if descriptor.is_configured() {
                return Err(IrqError::Busy);
            }

            if let Some(descriptor) = Arc::get_mut(descriptor) {
                break descriptor.realloc(sharing, data, flow);
            }

            drop(table);
            spin_loop();
        };

        // 旧 Placeholder 的析构可能释放内存，不放在 descriptor 表锁内执行。

        Ok(())
    }
}
