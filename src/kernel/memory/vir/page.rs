use core::{num::NonZeroUsize, pin::Pin, ptr::NonNull};

use crate::{
    arch::x86::kernel::page::PAGE_SIZE,
    kernel::memory::phy::kmalloc::kmalloc,
    lib::rust::{
        list::ListHead,
        rbtree::{
            augment::{Augment, ChangeSide},
            linked::LinkedRbNodeBase,
        },
    },
    linked_augment,
};

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
}

pub struct VirtPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
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
    pub const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        VirtPages {
            rb_node: LinkedRbNodeBase::linked_new(range, count),
        }
    }

    pub const fn get_start_addr(&self) -> NonZeroUsize {
        let addr = self.rb_node.get_key().start.get().get() * PAGE_SIZE;
        unsafe { NonZeroUsize::new_unchecked(addr) }
    }

    /// 从当前 VirtPages 中切出 count 个页，剩余部分加入 pool 链表
    /// 修改当前节点范围为 [start, start+count-1]，创建新节点 [start+count, old_end]
    pub(super) unsafe fn split_to_pool(
        &mut self,
        count: NonZeroUsize,
        mut list_head: Pin<&mut ListHead<LinkedRbNodeBase<VmRange, usize>>>,
    ) -> Option<()> {
        let range = self.rb_node.get_key();
        let old_start = range.start.get().get();
        let old_end = range.end;

        // 计算分割点：[old_start, split_point-1] 用于分配，[split_point, old_end] 放回 pool
        let split_point = old_start + count.get();

        // 修改当前节点范围
        unsafe {
            self.rb_node.get_key_mut().end =
                PageNumber::new(NonZeroUsize::new_unchecked(split_point - 1));
        }

        // 分配新节点存储剩余部分并加入链表
        let mut remainder =
            kmalloc::<VirtPages>(unsafe { NonZeroUsize::new_unchecked(size_of::<VirtPages>()) })?;

        unsafe {
            remainder.write(VirtPages::new(VmRange {
                start: PageNumber::new(NonZeroUsize::new_unchecked(split_point)),
                end: old_end,
            }));

            let node = Pin::new_unchecked(&mut remainder.as_mut().rb_node.augment.list_node);
            list_head.add_tail(node);
        }

        Some(())
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
