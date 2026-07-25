use core::{mem, num::NonZeroUsize, ptr::NonNull};

use alloc::boxed::Box;

use crate::{
    arch::{ArchPageTable, VirtAddr},
    kernel::memory::{
        MemoryError, PageCacheType, VMALLOC_BASE, VMALLOC_END,
        arch::ArchMemory,
        frame::{
            Frame,
            reference::{SharedFrames, UniqueFrames},
        },
        kmalloc::{Kernel, Kmalloc},
        page::{
            PageFlags, PageNumber, PageTableError, PageTableOps, current_root_pt, range::VmRange,
            vmap::get_vmap,
        },
    },
    lib::rust::rbtree::linked::LinkedRbNodeBase,
    linked_augment,
};

pub(in crate::kernel::memory) struct VmapNode {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    pub(super) frame_count: usize,
}

impl VmapNode {
    const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        Self {
            rb_node: LinkedRbNodeBase::linked_new(range, count),
            frame_count: 0,
        }
    }

    pub const fn fixed(start: PageNumber, count: NonZeroUsize) -> Self {
        let range = VmRange {
            start,
            end: start + count.get() - 1,
        };
        Self::new(range)
    }

    /// 获取内核可用临时虚拟地址空间的范围
    pub const fn kernel() -> Self {
        let (start, end) = (VMALLOC_BASE, VMALLOC_END);

        let start = start.to_page_number();
        let end = end.to_page_number();

        let vm_range = VmRange { start, end };

        Self::new(vm_range)
    }

    #[inline]
    pub fn start_addr(&self) -> VirtAddr {
        let addr = self.rb_node.get_key().start.get() * ArchPageTable::PAGE_SIZE;
        VirtAddr::new(addr)
    }

    /// 从当前空闲区间头部切出 `count` 页。
    ///
    /// # Safety
    ///
    /// 调用时节点必须已经从 intrusive tree/list 摘除。
    pub(super) unsafe fn split(&mut self, count: NonZeroUsize) -> Option<Box<VmapNode, Kmalloc>> {
        let range = self.rb_node.get_key();
        debug_assert!(count.get() < range.get_count());

        let old_start = range.start;
        let split_point = old_start + count.get();
        let allocated =
            Box::try_new_in(Self::fixed(old_start, count), Kmalloc::<Kernel>::default()).ok()?;

        let range = unsafe { self.rb_node.get_key_mut() };
        range.start = split_point;
        linked_augment!(self.rb_node) = range.get_count();

        Some(allocated)
    }
}

pub struct DynPages {
    pointer: NonNull<VmapNode>,
}

impl DynPages {
    /// # Safety
    ///
    /// `pointer` 在该对象使用期间必须有效，且不能同时存在另一个 `DynPages` 权限对象。
    /// 若允许该对象执行 Drop，节点还必须由 Vmap 持有并位于 allocated tree 中。
    pub(in crate::kernel::memory) const unsafe fn new(pointer: NonNull<VmapNode>) -> Self {
        Self { pointer }
    }

    pub fn start_addr(&self) -> VirtAddr {
        unsafe { self.pointer.as_ref().start_addr() }
    }

    pub const fn frame_count(&self) -> usize {
        unsafe { self.pointer.as_ref().frame_count }
    }

    pub fn map(
        &mut self,
        mut frame: UniqueFrames,
        cache_type: PageCacheType,
    ) -> Result<(), MemoryError> {
        // 由于vmap只使用range.start做比较，所以修改end不会影响树结构
        let count = frame.order().to_count().get();

        let offset = self.frame_count();

        PageTableOps::<ArchPageTable>::map(
            current_root_pt(),
            self,
            offset,
            &mut frame,
            PageFlags::new().cache_type(cache_type),
        )?;

        mem::forget(frame);

        let range = unsafe { self.pointer.as_ref().rb_node.get_key() };
        if offset + count > range.get_count() {
            printk!(
                "WARNING: DynPages range insufficient: required {}, available {}",
                offset + count,
                range.get_count()
            );
        }

        unsafe {
            self.pointer.as_mut().frame_count += count;
        }

        Ok(())
    }

    pub fn unmap(&mut self) -> Result<(), PageTableError> {
        let mut page_number = self.start_addr().to_page_number();
        let mut offset = 0;

        while offset < self.frame_count() {
            let vaddr = page_number.to_addr();
            let paddr = PageTableOps::<ArchPageTable>::translate(current_root_pt(), vaddr).unwrap();

            let frame_number = paddr.to_frame_number();
            let frame = Frame::get_raw(frame_number);

            let order;
            if let Some(unique) = unsafe { UniqueFrames::try_from_raw(frame) } {
                order = unique.order();
                page_number += order.to_count().get();

                PageTableOps::<ArchPageTable>::unmap(current_root_pt(), self, offset, order)
            } else if let Some(shared) = unsafe { SharedFrames::from_raw(frame) } {
                order = shared.order();
                page_number += order.to_count().get();

                PageTableOps::<ArchPageTable>::unmap(current_root_pt(), self, offset, order)
            } else {
                unreachable!(
                    "unmap failed: frame at {} is neither unique nor shared",
                    frame_number
                );
            }
            .inspect_err(|error| {
                printk!(
                    "unmap range failed! error: {:?}, start: {}, offset: {}, order: {:?}\n",
                    error,
                    self.start_addr(),
                    offset,
                    order
                );
            })?;
            offset += order.to_count().get();
        }

        unsafe {
            self.pointer.as_mut().frame_count = 0;
        }
        Ok(())
    }
}

impl Drop for DynPages {
    fn drop(&mut self) {
        let start = self.start_addr();
        if let Err(error) = self.unmap() {
            printk!(
                "WARNING: failed to release DynPages at {:?}: {:?}; keeping VmapNode allocated\n",
                start,
                error
            );
            return;
        }

        let mut pointer = self.pointer;
        if let Err(error) = get_vmap().as_mut().deallocate(unsafe { pointer.as_mut() }) {
            printk!(
                "WARNING: failed to return VmapNode at {:?}: {:?}\n",
                start,
                error
            );
        }
    }
}
