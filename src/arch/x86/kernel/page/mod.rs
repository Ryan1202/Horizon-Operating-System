use crate::kernel::memory::page::PageTableEntry;

pub type VirAddr = usize;
pub type PhyAddr = usize;

const PAGE_SIZE: usize = 4096;

type EntryType = usize;

#[repr(transparent)]
pub struct PageLevelEntry(EntryType);

impl PageTableEntry for PageLevelEntry {
    type PhyAddr = PhyAddr;

    const MAX: usize = PhyAddr::MAX.div_ceil(PAGE_SIZE);

    fn new_absent() -> Self {
        PageLevelEntry(0)
    }

    fn new_present(phy_addr: Self::PhyAddr) -> Self {
        PageLevelEntry(phy_addr as PhyAddr | 0x1)
    }

    fn is_present(&self) -> bool {
        (self.0 & 0x1) == 1
    }

    fn phy_addr(&self) -> Self::PhyAddr {
        ((self.0 as usize) & !(PAGE_SIZE - 1)) as PhyAddr
    }
}

pub const ENTRY_PER_PAGE: usize = PAGE_SIZE / size_of::<EntryType>();
