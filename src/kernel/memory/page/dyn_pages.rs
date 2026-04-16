use core::{
    mem::{self},
    num::NonZeroUsize,
    ptr::NonNull,
};

use crate::{
    arch::{ArchPageTable, VirtAddr},
    kernel::memory::{
        MemoryError, PageCacheType, VMALLOC_BASE, VMALLOC_END,
        arch::ArchMemory,
        frame::{
            Frame, FrameTag,
            buddy::FrameOrder,
            reference::{UniqueFrames, try_free},
        },
        kmalloc::kmalloc,
        page::{
            PageFlags, PageTable, PageTableError, PageTableOps, current_root_pt,
            lock::{NormalPtLock, PtRwLock},
            range::VmRange,
        },
    },
    lib::rust::rbtree::linked::LinkedRbNodeBase,
};

pub struct DynPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    pub(super) frame_count: usize,
    pub(super) head_frame: Option<UniqueFrames>,
}

impl DynPages {
    const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        DynPages {
            rb_node: LinkedRbNodeBase::linked_new(range, count),
            frame_count: 0,
            head_frame: None,
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

    pub fn map<W: PageTable>(
        &mut self,
        mut frame: UniqueFrames,
        cache_type: PageCacheType,
    ) -> Result<(), MemoryError>
    where
        for<'a> NormalPtLock: PtRwLock<'a, W>,
    {
        // 由于vmap只使用range.start做比较，所以修改end不会影响树结构
        let tag = frame.get_tag();
        let count = frame.get_order().to_count().get();

        match tag {
            FrameTag::Anonymous => unsafe { frame.get_data_mut().anonymous.acquire() },
            FrameTag::AssignedFixed => unsafe { frame.get_data_mut().assigned.acquire() },
            _ => return Err(MemoryError::UnavailableFrame),
        }

        PageTableOps::<W>::map(
            current_root_pt(),
            self,
            self.frame_count,
            &mut frame,
            PageFlags::new().cache_type(cache_type),
        )
        .inspect_err(|_| match tag {
            FrameTag::Anonymous => unsafe {
                frame.get_data().anonymous.release();
            },
            FrameTag::AssignedFixed => unsafe {
                frame.get_data().assigned.release();
            },
            _ => unreachable!(),
        })?;
        {
            let range = self.rb_node.get_key();

            if self.head_frame.is_none() {
                self.head_frame = Some(frame);
            } else {
                mem::forget(frame);
            }

            if self.frame_count + count > range.get_count() {
                printk!(
                    "WARNING: DynPages range insufficient: required {}, available {}",
                    self.frame_count + count,
                    range.get_count()
                );
            }
        }

        self.frame_count += count;

        Ok(())
    }

    pub fn unmap<W: PageTable>(&mut self) -> Result<(), PageTableError>
    where
        for<'a> NormalPtLock: PtRwLock<'a, W>,
    {
        let mut page_number = self.start_addr().to_page_number();
        let mut offset = 0;

        while offset < self.frame_count {
            let vaddr = page_number.to_addr();
            let paddr = PageTableOps::<W>::translate(current_root_pt(), vaddr).unwrap();
            let frame_number = paddr.to_frame_number();
            let frame = unsafe { Frame::get_raw(frame_number).as_ref() };

            let (next, order) = match frame.get_tag() {
                FrameTag::Anonymous => {
                    let order = unsafe { frame.get_data().anonymous.get_order() };
                    (page_number + order.to_count().get(), order)
                }
                FrameTag::AssignedFixed => {
                    let order = unsafe { frame.get_data().assigned.get_order() };
                    (page_number + order.to_count().get(), order)
                }
                _ => {
                    let tag = Frame::get_tag_relaxed(frame_number + 1);
                    if let FrameTag::Tail = tag {
                        let frame = unsafe { Frame::get_raw(frame_number + 1).as_ref() };

                        let range = unsafe { frame.get_data().range };
                        let count = range.end.count_from(range.start);

                        let next = page_number + count;
                        let order = FrameOrder::from_count(count);

                        (next, order)
                    } else {
                        (page_number + 1, FrameOrder::new(0))
                    }
                }
            };
            page_number = next;

            let _ =
                PageTableOps::<W>::unmap(current_root_pt(), self, offset, order).inspect_err(|e| {
                    printk!(
                        "unmap range failed! error: {:?}, start: {}, offset: {}, order: {:?}\n",
                        e,
                        self.start_addr(),
                        offset,
                        order
                    )
                });

            // 最后释放
            unsafe {
                try_free(Frame::get_raw(frame_number));
            }
            offset += order.to_count().get();
        }

        self.frame_count = 0;
        Ok(())
    }
}
