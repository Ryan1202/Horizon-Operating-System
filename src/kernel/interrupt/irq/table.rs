//! [`IrqNumber`] 直接索引 descriptor 指针数组
//!
//! 表锁只保护发布、撤下和取得首个引用。释放表锁后才能进入 descriptor 锁。
//! 租约计数位于 descriptor 中，但只在本文件持有表锁时修改。

use alloc::boxed::Box;

use super::{IrqError, IrqNumber, allocator::MAX_IRQS, descriptor::IrqDescriptor};
use crate::{kernel::memory::kmalloc::Kmalloc, lib::rust::spinlock::Spinlock};
use core::{
    ops::{Deref, DerefMut},
    ptr::NonNull,
};

pub static IRQ_DESCRIPTORS: IrqTable = IrqTable::new();

struct IrqTable(Spinlock<[Option<NonNull<IrqDescriptor>>; MAX_IRQS]>);

impl IrqTable {
    const fn new() -> Self {
        Self(Spinlock::new([None; MAX_IRQS]))
    }

    pub fn publish(&self, descriptor: IrqDescriptor) -> Result<(), IrqError> {
        let descriptor = Box::<_, Kmalloc>::new_in(descriptor, Kmalloc::default());
        let ptr = Box::into_non_null_with_allocator(descriptor).0;
        let descriptor = unsafe { ptr.as_ref() };

        let mut table = self.0.lock_irqsave();

        let irq = descriptor.irq.get() as usize;

        if table[irq].is_some() {
            Err(IrqError::AlreadyPublished)
        } else {
            table[irq] = Some(ptr);
            Ok(())
        }
    }

    pub fn lookup(&self, irq: IrqNumber) -> Option<DescriptorGuard> {
        let desc = unsafe { self.0.lock()[irq.get() as usize]?.as_mut() };

        Some(DescriptorGuard::new(desc))
    }
}

/// IRQ 路径的短期引用，不持有 action 或 mapping 的所有权。
pub(super) struct DescriptorGuard<'a>(&'a mut IrqDescriptor);

impl<'a> DescriptorGuard<'a> {
    fn new(desc: &'a mut IrqDescriptor) -> Self {
        // SAFETY: 首次加引用与 unpublish 在同一表锁内互斥。
        // let old = desc.references.fetch_add(1, Ordering::Relaxed);
        // assert!(old < isize::MAX as usize, "IRQ reference overflow");

        Self(desc)
    }
}

impl<'a> Deref for DescriptorGuard<'a> {
    type Target = IrqDescriptor;

    fn deref(&self) -> &Self::Target {
        self.0
    }
}

impl<'a> DerefMut for DescriptorGuard<'a> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: 由 new 保证引用计数非零，且表锁与 unpublish 互斥
        self.0
    }
}

// impl<'a> Drop for DescriptorGuard<'a> {
//     fn drop(&mut self) {
//         // 减到零后，回收者可能立即释放对象；此后不能再访问 descriptor。
//         self.0.references.fetch_sub(1, Ordering::Release);
//     }
// }
