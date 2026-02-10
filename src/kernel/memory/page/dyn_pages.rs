use core::{
    fmt::Write,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ptr::{NonNull, addr_of},
};

use crate::{
    ConsoleOutput,
    arch::x86::kernel::page::PAGE_SIZE,
    kernel::memory::{
        KLINEAR_SIZE, PageCacheType, VIR_BASE,
        frame::reference::FrameMut,
        kmalloc::kmalloc,
        page::{PageNumber, range::VmRange},
        page_link, page_unlink,
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
        let (start, end) = (addr_of!(VIR_BASE) as usize + KLINEAR_SIZE, 0xff80_0000);
        let start = PageNumber::from_addr(start).unwrap();
        let end = PageNumber::from_addr(end).unwrap();

        let vm_range = VmRange { start, end };

        Self::new(vm_range)
    }

    pub const fn start_addr(&self) -> NonZeroUsize {
        let addr = self.rb_node.get_key().start.get().get() * PAGE_SIZE;
        unsafe { NonZeroUsize::new_unchecked(addr) }
    }

    /// 从当前 VirtPages 中切出 count 个页
    /// 修改当前节点范围为 [start+count, end]，创建新节点 [start, start+count-1] 并返回
    pub(super) unsafe fn split(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
        let range = self.rb_node.get_key();
        let old_start = range.start;

        // 计算分割点：[old_start, split_point-1] 用于分配，[split_point, old_end] 放回 pool
        let split_point = old_start.get().get() + count.get();

        // 修改当前节点范围
        unsafe {
            self.rb_node.get_key_mut().start =
                PageNumber::new(NonZeroUsize::new_unchecked(split_point));
        }

        // 分配新节点存储分配部分
        let allocated =
            kmalloc::<DynPages>(unsafe { NonZeroUsize::new_unchecked(size_of::<DynPages>()) })?;

        unsafe {
            allocated.write(DynPages::new(VmRange {
                start: old_start,
                end: PageNumber::new(NonZeroUsize::new_unchecked(split_point - 1)),
            }));
        }

        Some(allocated)
    }

    pub fn link(&mut self, frame: FrameMut, count: usize, cache_type: PageCacheType) -> Option<()> {
        unsafe {
            // 由于vmap只使用range.start做比较，所以修改end不会影响树结构
            let range = self.rb_node.get_key();

            let start_addr = frame.start_addr();

            if self.first_frame.is_none() {
                self.first_frame = Some(frame);
            } else {
                let _ = ManuallyDrop::new(frame);
            }

            if self.frame_count + count > range.get_count() {
                let mut output = ConsoleOutput;
                writeln!(
                    output,
                    "VirtPages range insufficient: required {}, available {}",
                    self.frame_count + count,
                    range.get_count()
                )
                .unwrap();
            }

            page_link(
                (range.start + self.frame_count).to_addr(),
                start_addr,
                count as u16,
                cache_type,
            )
            .then_some({
                self.frame_count += count;
            })
        }
    }

    pub fn unlink(&mut self) {
        unsafe {
            let range = self.rb_node.get_key();

            page_unlink(range.start.to_addr(), self.frame_count as u16);

            self.frame_count = 0;
        }
    }
}
