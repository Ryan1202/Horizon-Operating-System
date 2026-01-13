use core::{num::NonZeroUsize, pin::Pin, ptr::NonNull};

use crate::{
    kernel::memory::phy::{kmalloc::kmalloc, page::PAGE_SIZE},
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
pub struct VirtPageNumber(NonZeroUsize);

impl VirtPageNumber {
    pub const fn new(num: NonZeroUsize) -> Self {
        VirtPageNumber(num)
    }

    pub const fn from_addr(addr: usize) -> Option<Self> {
        match NonZeroUsize::new(addr / PAGE_SIZE) {
            Some(pn) => Some(VirtPageNumber(pn)),
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
}

pub struct VmRange {
    pub start: VirtPageNumber,
    pub end: VirtPageNumber,
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

impl VirtPages {
    pub(super) unsafe fn split_to_pool(
        &mut self,
        count: NonZeroUsize,
        list_head: Pin<&mut ListHead<LinkedRbNodeBase<VmRange, usize>>>,
    ) -> Option<()> {
        let old_end = self.rb_node.get_key().end;
        let end = self.rb_node.get_key().start.get().get() + count.get();
        unsafe {
            self.rb_node.get_key_mut().end =
                VirtPageNumber::new(NonZeroUsize::new_unchecked(end - 1));

            let mut new_vpages =
                kmalloc::<VirtPages>(NonZeroUsize::new_unchecked(size_of::<VirtPages>()))?;

            new_vpages.write(VirtPages::new(VmRange {
                start: VirtPageNumber::new(NonZeroUsize::new_unchecked(end)),
                end: old_end,
            }));

            let node = Pin::new_unchecked(&mut new_vpages.as_mut().rb_node.augment.list_node);

            list_head.add_tail(node);
        }
        Some(())
    }
}
