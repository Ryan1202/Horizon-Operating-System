use crate::{arch::PhyAddr, kernel::memory::page::PageTableEntry};

pub mod addr;

pub const PAGE_SIZE: usize = 4096;

type EntryType = usize;

#[repr(transparent)]
pub struct PageLevelEntry(EntryType);

impl PageTableEntry for PageLevelEntry {
    const MAX: usize = PhyAddr::MAX.div_ceil(PAGE_SIZE);

    fn new_absent() -> Self {
        PageLevelEntry(0)
    }

    fn new_present(phy_addr: PhyAddr) -> Self {
        PageLevelEntry(phy_addr.page_align_down().as_usize() | 0x1)
    }

    fn is_present(&self) -> bool {
        (self.0 & 0x1) == 1
    }

    fn phy_addr(&self) -> PhyAddr {
        PhyAddr::new((self.0 as usize) & !(PAGE_SIZE - 1))
    }
}

pub const ENTRY_PER_PAGE: usize = PAGE_SIZE / size_of::<EntryType>();
