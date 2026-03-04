use core::{
    num::NonZeroUsize,
    ptr::{with_exposed_provenance, with_exposed_provenance_mut},
};

use crate::{
    arch::ArchPageTable,
    impl_page_addr,
    kernel::memory::{arch::ArchMemory, frame::FrameNumber, page::PageNumber},
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct PhysAddr(usize);

impl PhysAddr {
    pub const MAX: usize = usize::MAX;

    pub const fn new(addr: usize) -> Self {
        Self(addr)
    }

    pub const fn as_usize(&self) -> usize {
        self.0
    }

    pub const fn to_frame_number(&self) -> FrameNumber {
        FrameNumber::new(self.0 / ArchPageTable::PAGE_SIZE)
    }

    pub const fn from_frame_number(frame_num: FrameNumber) -> Self {
        Self(frame_num.get() * ArchPageTable::PAGE_SIZE)
    }
}

impl_page_addr!(PhysAddr, ArchPageTable::PAGE_SIZE);

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct VirtAddr(usize);

impl VirtAddr {
    pub const fn new(addr: usize) -> Self {
        Self(addr)
    }

    pub const fn as_usize(&self) -> usize {
        self.0
    }

    pub const fn to_page_number(&self) -> Option<PageNumber> {
        match NonZeroUsize::new(self.0 / ArchPageTable::PAGE_SIZE) {
            Some(x) => Some(PageNumber::new(x)),
            None => None,
        }
    }

    pub const fn as_ptr<T>(self) -> *const T {
        with_exposed_provenance(self.0)
    }

    pub const fn as_mut_ptr<T>(self) -> *mut T {
        with_exposed_provenance_mut(self.0)
    }
}

impl_page_addr!(VirtAddr, ArchPageTable::PAGE_SIZE);
