use core::{cmp::Ordering, ptr::NonNull};

use crate::{
    kernel::memory::page::PageNumber,
    lib::rust::rbtree::{
        augment::{Augment, ChangeSide},
        linked::LinkedRbNodeBase,
    },
    linked_augment,
};

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
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.start.get().cmp(&other.start.get()))
    }
}

impl Ord for VmRange {
    fn cmp(&self, other: &Self) -> Ordering {
        self.start.get().cmp(&other.start.get())
    }
}

impl VmRange {
    pub fn cmp_range(&self, other: &Self) -> Ordering {
        if self.end.get() < other.start.get() {
            Ordering::Less
        } else if self.start.get() > other.end.get() {
            Ordering::Greater
        } else {
            Ordering::Equal
        }
    }

    pub const fn get_count(&self) -> usize {
        self.end.get().get() - self.start.get().get() + 1
    }
}

impl Augment for LinkedRbNodeBase<VmRange, usize> {
    fn recalc(&mut self, side: ChangeSide) {
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
