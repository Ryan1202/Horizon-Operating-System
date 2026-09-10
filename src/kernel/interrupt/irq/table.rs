//! 指针表与占位替换共用表锁。占位对象的借用不能逃出锁

use alloc::boxed::Box;
use core::{any::Any, ops::Deref, ptr::NonNull};

use super::{Domain, Flow, IrqError, IrqNumber, allocator::MAX_IRQS, descriptor::IrqDescriptor};
use crate::{
    kernel::memory::kmalloc::Kmalloc,
    lib::rust::spinlock::{SpinIrqGuard, Spinlock},
};

type Descriptors = [Option<NonNull<IrqDescriptor>>; MAX_IRQS];

pub static IRQ_DESCRIPTORS: IrqTable = IrqTable::new();

pub struct IrqTable(Spinlock<Descriptors>);

impl IrqTable {
    const fn new() -> Self {
        Self(Spinlock::new([None; MAX_IRQS]))
    }

    pub(super) fn publish(&self, descriptor: IrqDescriptor) -> Result<(), IrqError> {
        let irq = descriptor.irq.get();
        let descriptor = Box::<_, Kmalloc>::new_in(descriptor, Kmalloc::default());

        let mut table = self.0.lock_irqsave();
        if table[irq].is_some() {
            // descriptor 在锁之前声明；返回时先解锁，再析构
            return Err(IrqError::AlreadyPublished);
        }

        table[irq] = Some(Box::into_non_null_with_allocator(descriptor).0);

        Ok(())
    }

    pub fn lookup(&self, irq: IrqNumber) -> Option<DescriptorGuard<'_>> {
        let table = self.0.lock_irqsave();
        let descriptor = table[irq.get()]?;

        Some(DescriptorGuard {
            descriptor,
            _table: table,
        })
    }

    pub fn realloc(
        &self,
        irq: IrqNumber,
        domain: &'static dyn Domain,
        flow: Flow,
        arg: &dyn Any,
    ) -> Result<(), IrqError> {
        super::assert_management();

        {
            let descriptor = self.lookup(irq).ok_or(IrqError::NotFound)?;
            if descriptor.is_configured() {
                return Err(IrqError::Busy);
            }
        }

        // 分配可能等待；构造完成后再取得表锁检查和提交
        let data = domain.allocate(irq, arg)?;
        let table = self.0.lock_irqsave();
        let mut ptr = table[irq.get()].ok_or(IrqError::NotFound)?;

        // SAFETY: 表锁阻止占位对象被借用或同时修改
        // 先用共享引用检查；已配置对象可能被 handle 持有，不得对它取得 &mut
        if unsafe { ptr.as_ref().is_configured() } {
            return Err(IrqError::Busy);
        }

        // SAFETY: 确认为占位；此时没有 action，也没有表锁外的共享借用
        let old = unsafe { ptr.as_mut().realloc(data, flow) };

        drop(table);
        drop(old);

        Ok(())
    }
}

/// 共享访问与表锁同寿命；持有此 guard 时不能再次进入表操作
pub struct DescriptorGuard<'a> {
    descriptor: NonNull<IrqDescriptor>,
    _table: SpinIrqGuard<'a, &'a mut Descriptors>,
}

impl Deref for DescriptorGuard<'_> {
    type Target = IrqDescriptor;

    fn deref(&self) -> &Self::Target {
        // SAFETY: 表锁保护占位内容，且 descriptor 在本阶段不回收
        unsafe { self.descriptor.as_ref() }
    }
}
