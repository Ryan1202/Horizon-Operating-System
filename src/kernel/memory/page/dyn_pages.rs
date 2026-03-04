use core::{
    fmt::Write,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ptr::{NonNull, addr_of},
};

use crate::{
    ConsoleOutput,
    arch::{ArchPageTable, VirtAddr},
    kernel::memory::{
        KLINEAR_SIZE, KMEMORY_END, MemoryError, PageCacheType, VIR_BASE,
        arch::ArchMemory,
        frame::reference::FrameMut,
        kmalloc::kmalloc,
        page::{PageFlags, PageTableError, PageTableWalker, range::VmRange},
    },
    lib::rust::rbtree::linked::LinkedRbNodeBase,
};

pub struct DynPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    pub(super) frame_count: usize,
    pub(super) first_frame: Option<FrameMut>,
}

impl DynPages {
    const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        DynPages {
            rb_node: LinkedRbNodeBase::linked_new(range, count),
            frame_count: 0,
            first_frame: None,
        }
    }

    #[inline(always)]
    pub fn kernel() -> Self {
        let start = addr_of!(VIR_BASE) as usize + KLINEAR_SIZE;
        let (start, end) = (VirtAddr::new(start), KMEMORY_END);

        let start = start.to_page_number().unwrap();
        let end = end.to_page_number().unwrap();

        let vm_range = VmRange { start, end };

        Self::new(vm_range)
    }

    pub const fn start_addr(&self) -> VirtAddr {
        let addr = self.rb_node.get_key().start.get().get() * ArchPageTable::PAGE_SIZE;
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

    pub fn link<W: PageTableWalker>(
        &mut self,
        frame: FrameMut,
        count: usize,
        cache_type: PageCacheType,
    ) -> Result<(), MemoryError> {
        // 由于vmap只使用range.start做比较，所以修改end不会影响树结构
        let frame_number = frame.to_frame_number();

        {
            let range = self.rb_node.get_key();

            if self.first_frame.is_none() {
                self.first_frame = Some(frame);
            } else {
                let _ = ManuallyDrop::new(frame);
            }

            if self.frame_count + count > range.get_count() {
                let mut output = ConsoleOutput;
                writeln!(
                    output,
                    "DynPages range insufficient: required {}, available {}",
                    self.frame_count + count,
                    range.get_count()
                )
                .unwrap();
            }
        }

        W::map_range(
            self,
            frame_number,
            self.frame_count,
            count,
            PageFlags::new().cache_type(cache_type),
        )?;
        self.frame_count += count;
        Ok(())
    }

    pub fn unlink<W: PageTableWalker>(&mut self) -> Result<(), PageTableError> {
        W::unmap_range(self, 0, self.frame_count)?;

        self.frame_count = 0;
        Ok(())
    }
}
