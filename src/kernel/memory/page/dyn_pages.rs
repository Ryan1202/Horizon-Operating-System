use core::{num::NonZeroUsize, ptr::NonNull};

use crate::{
    arch::{ArchPageTable, VirtAddr},
    kernel::memory::{
        MemoryError, PageCacheType, VMALLOC_BASE, VMALLOC_END,
        arch::ArchMemory,
        frame::{
            Frame,
            reference::{SharedFrames, UniqueFrames},
        },
        kmalloc::kmalloc,
        page::{PageFlags, PageTableError, PageTableOps, current_root_pt, range::VmRange},
    },
    lib::rust::rbtree::linked::LinkedRbNodeBase,
};

pub struct DynPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    pub(super) frame_count: usize,
}

impl DynPages {
    const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        DynPages {
            rb_node: LinkedRbNodeBase::linked_new(range, count),
            frame_count: 0,
        }
    }

    pub const fn fixed(
        start: crate::kernel::memory::page::PageNumber,
        count: NonZeroUsize,
    ) -> Self {
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

    pub const fn start_addr(&self) -> VirtAddr {
        let addr = self.rb_node.get_key().start.get() * ArchPageTable::PAGE_SIZE;
        VirtAddr::new(addr)
    }

    /// 从当前 VirtPages 中切出 count 个页
    /// 修改当前节点范围为 [start+count, end]，创建新节点 [start, start+count-1] 并返回
    pub(super) unsafe fn split(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
        let range = self.rb_node.get_key();
        let old_start = range.start;

        // 计算分割点：[old_start, split_point-1] 用于分配，[split_point, old_end] 放回 pool
        let split_point = old_start + count.get();

        // 修改当前节点范围
        unsafe {
            self.rb_node.get_key_mut().start = split_point;
        }

        // 分配新节点存储分配部分
        let allocated =
            kmalloc::<DynPages>(unsafe { NonZeroUsize::new_unchecked(size_of::<DynPages>()) })?;

        unsafe {
            allocated.write(DynPages::new(VmRange {
                start: old_start,
                end: split_point - 1,
            }));
        }

        Some(allocated)
    }

    pub fn map(
        &mut self,
        frame: &mut UniqueFrames,
        cache_type: PageCacheType,
    ) -> Result<(), MemoryError> {
        // 由于vmap只使用range.start做比较，所以修改end不会影响树结构
        let count = frame.order().to_count().get();

        PageTableOps::<ArchPageTable>::map(
            current_root_pt(),
            self,
            self.frame_count,
            frame,
            PageFlags::new().cache_type(cache_type),
        )?;

        let range = self.rb_node.get_key();
        if self.frame_count + count > range.get_count() {
            printk!(
                "WARNING: DynPages range insufficient: required {}, available {}",
                self.frame_count + count,
                range.get_count()
            );
        }

        self.frame_count += count;

        Ok(())
    }

    pub fn unmap(&mut self) -> Result<(), PageTableError> {
        let mut page_number = self.start_addr().to_page_number();
        let mut offset = 0;

        while offset < self.frame_count {
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
            .inspect_err(|e| {
                printk!(
                    "unmap range failed! error: {:?}, start: {}, offset: {}, order: {:?}\n",
                    e,
                    self.start_addr(),
                    offset,
                    order
                );
            })?;
            offset += order.to_count().get();
        }

        self.frame_count = 0;
        Ok(())
    }
}
