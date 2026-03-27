use crate::{
    arch::ArchPageTable,
    kernel::memory::{
        PageCacheType,
        arch::ArchMemory,
        frame::FrameNumber,
        page::{PageEntry, PageFlags},
    },
};

struct X86PageFlags(u32);

impl X86PageFlags {
    const PRESENT: u32 = 1 << 0;
    const WRITABLE: u32 = 1 << 1;
    const USER: u32 = 1 << 2;
    const ACCESSED: u32 = 1 << 5;
    const DIRTY: u32 = 1 << 6;
    const PAGE_SIZE: u32 = 1 << 7;
    const GLOBAL: u32 = 1 << 8;

    const PWT: u32 = 1 << 3; // Page-level Write-Through
    const PCD: u32 = 1 << 4; // Page-level Cache Disable
    const PAT_PTE: u32 = 1 << 7; // Page Attribute Table (only in PTE)
    const PAT_PDE: u32 = 1 << 12;

    const CACHE_WRITE_BACK: u32 = 0;
    const CACHE_WRITE_COMBINE_PTE: u32 = Self::PAT_PTE | Self::PWT;
    const CACHE_WRITE_COMBINE_PDE: u32 = Self::PAT_PDE | Self::PWT;
    const CACHE_WRITE_THROUGH: u32 = Self::PWT;
    const CACHE_UNCACHED: u32 = Self::PCD | Self::PWT;
    const CACHE_UNCACHED_MINUS: u32 = Self::PCD;

    const fn as_page_flags(&self, is_pte: bool) -> PageFlags {
        let pat = if is_pte {
            (self.0 & Self::PAT_PTE) != 0
        } else {
            (self.0 & Self::PAT_PDE) != 0
        };
        let pwt = (self.0 & Self::PWT) != 0;
        let pcd = (self.0 & Self::PCD) != 0;

        let cache_type = match (pat, pwt, pcd) {
            (_, false, false) => PageCacheType::WriteBack,
            (true, true, false) => PageCacheType::WriteCombine,
            (_, true, false) => PageCacheType::WriteThrough,
            (_, false, true) => PageCacheType::Uncached,
            (_, true, true) => PageCacheType::UncachedMinus,
        };

        PageFlags {
            present: (self.0 & Self::PRESENT) != 0,
            writable: (self.0 & Self::WRITABLE) != 0,
            user: (self.0 & Self::USER) != 0,
            no_execute: false, // x86 不支持 NX 位
            global: (self.0 & Self::GLOBAL) != 0,
            accessed: (self.0 & Self::ACCESSED) != 0,
            dirty: (self.0 & Self::DIRTY) != 0,
            huge_page: (self.0 & Self::PAGE_SIZE) != 0,
            cache_type,
        }
    }
}

impl From<PageFlags> for X86PageFlags {
    fn from(flags: PageFlags) -> Self {
        let mut x86_flags = 0;

        if flags.present {
            x86_flags |= Self::PRESENT;
        }
        if flags.writable {
            x86_flags |= Self::WRITABLE;
        }
        if flags.user {
            x86_flags |= Self::USER;
        }
        // if flags.accessed { x86_flags |= Self::ACCESSED; }
        // if flags.dirty { x86_flags |= Self::DIRTY; }
        if flags.huge_page {
            x86_flags |= Self::PAGE_SIZE;
        }
        if flags.global {
            x86_flags |= Self::GLOBAL;
        }

        x86_flags |= match flags.cache_type {
            PageCacheType::WriteBack => Self::CACHE_WRITE_BACK,
            PageCacheType::WriteCombine => {
                if flags.huge_page {
                    Self::CACHE_WRITE_COMBINE_PDE
                } else {
                    Self::CACHE_WRITE_COMBINE_PTE
                }
            }
            PageCacheType::WriteThrough => Self::CACHE_WRITE_THROUGH,
            PageCacheType::Uncached => Self::CACHE_UNCACHED,
            PageCacheType::UncachedMinus => Self::CACHE_UNCACHED_MINUS,
        };

        Self(x86_flags)
    }
}

#[repr(transparent)]
pub struct X86PageEntry(pub(super) u32);

impl PageEntry for X86PageEntry {
    fn new_absent() -> Self {
        X86PageEntry(0)
    }

    fn new_mapped(frame: FrameNumber, flags: PageFlags, page_level: u8) -> Self {
        let mut entry = Self((frame.get() * ArchPageTable::PAGE_SIZE) as u32);

        let flags = if page_level == 0 {
            flags.huge_page(false)
        } else {
            flags
        };
        entry.set_flags(flags);
        entry
    }

    fn is_present(&self) -> bool {
        (self.0 & 0x1) == 1
    }

    fn frame_number(&self) -> Option<FrameNumber> {
        if self.is_present() {
            Some(FrameNumber::new(self.0 as usize / ArchPageTable::PAGE_SIZE))
        } else {
            None
        }
    }

    fn flags(&self, page_level: u8) -> PageFlags {
        X86PageFlags(self.0 & 0xFFF).as_page_flags(page_level == 0)
    }

    fn set_flags(&mut self, flags: PageFlags) {
        let flags = X86PageFlags::from(flags);
        self.0 = (self.0 & !0xFFF) | flags.0;
    }
}
