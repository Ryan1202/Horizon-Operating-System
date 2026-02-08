use core::{
    fmt::Write,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ops::{Add, Sub},
    ptr::{NonNull, addr_of, with_exposed_provenance_mut},
};

use crate::{
    ConsoleOutput,
    arch::x86::kernel::page::PAGE_SIZE,
    kernel::memory::{
        KLINEAR_SIZE, PageCacheType, VIR_BASE, page_link, page_unlink,
        phy::{frame::reference::FrameMut, kmalloc::kmalloc},
        vir_base_addr,
    },
    lib::rust::rbtree::{
        augment::{Augment, ChangeSide},
        linked::LinkedRbNodeBase,
    },
    linked_augment,
};

pub mod options;

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct PageNumber(NonZeroUsize);

impl PageNumber {
    pub const fn new(num: NonZeroUsize) -> Self {
        PageNumber(num)
    }

    pub const fn from_addr(addr: usize) -> Option<Self> {
        match NonZeroUsize::new(addr / PAGE_SIZE) {
            Some(pn) => Some(PageNumber(pn)),
            None => None,
        }
    }

    pub const fn get(&self) -> NonZeroUsize {
        self.0
    }

    pub const fn to_addr(&self) -> usize {
        self.0.get() * PAGE_SIZE
    }
}

impl Add<usize> for PageNumber {
    type Output = Self;

    fn add(self, rhs: usize) -> Self::Output {
        PageNumber(unsafe { NonZeroUsize::new_unchecked(self.0.get() + rhs) })
    }
}

impl Sub<usize> for PageNumber {
    type Output = Self;

    fn sub(self, rhs: usize) -> Self::Output {
        PageNumber(unsafe { NonZeroUsize::new_unchecked(self.0.get() - rhs) })
    }
}

pub struct VirtPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    frame_count: usize,
    first_frame: Option<FrameMut>,
}

impl Augment for LinkedRbNodeBase<VmRange, usize> {
    fn recalc(&mut self, side: crate::lib::rust::rbtree::augment::ChangeSide) {
        let size = self.get_key().get_count();
        let max_size = &mut linked_augment!(self);

        *max_size = size;
        match side {
            ChangeSide::Left => {
                if let Some(left) = self.left {
                    let left_ref = unsafe { left.as_ref() };
                    *max_size = (*max_size).max(linked_augment!(left_ref));
                }
            }
            ChangeSide::Right => {
                if let Some(right) = self.right {
                    let right_ref = unsafe { right.as_ref() };
                    *max_size = (*max_size).max(linked_augment!(right_ref));
                }
            }
            ChangeSide::Both => {
                if let Some(left) = self.left {
                    let left_ref = unsafe { left.as_ref() };
                    *max_size = (*max_size).max(linked_augment!(left_ref));
                }
                if let Some(right) = self.right {
                    let right_ref = unsafe { right.as_ref() };
                    *max_size = (*max_size).max(linked_augment!(right_ref));
                }
            }
        }
    }

    fn propagate(&mut self, root: NonNull<Self>) {
        let mut cur = Some(self);
        while let Some(c) = cur {
            c.recalc(ChangeSide::Both);
            if NonNull::from_ref(c) == root {
                break;
            }
            cur = c.get_parent().map(|mut p| unsafe { p.as_mut() });
        }
    }
}

impl VirtPages {
    const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        VirtPages {
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
    pub(super) unsafe fn split(&mut self, count: NonZeroUsize) -> Option<NonNull<VirtPages>> {
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
            kmalloc::<VirtPages>(unsafe { NonZeroUsize::new_unchecked(size_of::<VirtPages>()) })?;

        unsafe {
            allocated.write(VirtPages::new(VmRange {
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

pub struct VmRange {
    pub start: PageNumber,
    pub end: PageNumber,
}

impl PartialEq for VmRange {
    fn eq(&self, other: &Self) -> bool {
        self.start.get() == other.start.get()
    }
}
impl Eq for VmRange {}

impl PartialOrd for VmRange {
    fn partial_cmp(&self, other: &Self) -> Option<core::cmp::Ordering> {
        Some(self.start.get().cmp(&other.start.get()))
    }
}

impl Ord for VmRange {
    fn cmp(&self, other: &Self) -> core::cmp::Ordering {
        self.start.get().cmp(&other.start.get())
    }
}

impl VmRange {
    pub fn cmp_range(&self, other: &Self) -> core::cmp::Ordering {
        if self.end.get() < other.start.get() {
            core::cmp::Ordering::Less
        } else if self.start.get() > other.end.get() {
            core::cmp::Ordering::Greater
        } else {
            core::cmp::Ordering::Equal
        }
    }

    pub const fn get_count(&self) -> usize {
        self.end.get().get() - self.start.get().get() + 1
    }
}

pub enum Pages<'a> {
    Fixed((FrameMut, usize)),
    Dynamic(&'a mut VirtPages),
}

impl<'a> Pages<'a> {
    pub fn start_addr(&self) -> NonZeroUsize {
        match self {
            Pages::Fixed((frame, _)) => unsafe {
                NonZeroUsize::new_unchecked(vir_base_addr() + frame.start_addr())
            },
            Pages::Dynamic(vpages) => vpages.start_addr(),
        }
    }

    pub fn get_ptr<T>(&mut self) -> *mut T {
        let addr = match self {
            Pages::Fixed((frame, _)) => frame.start_addr(),
            Pages::Dynamic(vpages) => vpages.start_addr().get(),
        };
        with_exposed_provenance_mut::<T>(addr)
    }

    pub fn get_frame(&mut self) -> Option<&mut FrameMut> {
        match self {
            Pages::Fixed((frame, _)) => Some(frame),
            Pages::Dynamic(vpages) => vpages.first_frame.as_mut(),
        }
    }

    pub fn get_count(&self) -> usize {
        match self {
            Pages::Fixed((_, count)) => *count,
            Pages::Dynamic(vpages) => vpages.frame_count,
        }
    }
}
