use core::sync::atomic::{AtomicUsize, Ordering};

use crate::{
    arch::{ArchPageTable, x86::kernel::page::table::HUGE_PAGE},
    kernel::memory::{
        PageCacheType,
        arch::ArchMemory,
        frame::FrameNumber,
        page::{PageFlags, PageTableEntry, PageTableEntrySlot},
    },
};

struct X86PageFlags(usize);

impl X86PageFlags {
    const PRESENT: usize = 1 << 0;
    const WRITABLE: usize = 1 << 1;
    const USER: usize = 1 << 2;
    const ACCESSED: usize = 1 << 5;
    const DIRTY: usize = 1 << 6;
    const PAGE_SIZE: usize = 1 << 7;
    const GLOBAL: usize = 1 << 8;

    const PWT: usize = 1 << 3; // Page-level Write-Through
    const PCD: usize = 1 << 4; // Page-level Cache Disable
    const PAT_PTE: usize = 1 << 7; // Page Attribute Table (only in PTE)
    const PAT_PDE: usize = 1 << 12;

    const CACHE_WRITE_BACK: usize = 0;
    const CACHE_WRITE_COMBINE: usize = Self::PAT_PTE | Self::PWT;
    const CACHE_WRITE_COMBINE_HUGE: usize = Self::PAT_PDE | Self::PWT;
    const CACHE_WRITE_THROUGH: usize = Self::PWT;
    const CACHE_UNCACHED: usize = Self::PCD | Self::PWT;
    const CACHE_UNCACHED_MINUS: usize = Self::PCD;

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

impl X86PageFlags {
    const fn from(flags: PageFlags, level: usize) -> Self {
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
        let huge_page = flags.huge_page && HUGE_PAGE[level];
        if huge_page {
            x86_flags |= Self::PAGE_SIZE;
        }
        if flags.global {
            x86_flags |= Self::GLOBAL;
        }

        x86_flags |= match flags.cache_type {
            PageCacheType::WriteBack => Self::CACHE_WRITE_BACK,
            PageCacheType::WriteCombine => {
                if level > 0 && huge_page {
                    // 对于大页，使用专门的组合
                    Self::CACHE_WRITE_COMBINE_HUGE
                } else if level == 0 {
                    // 对于小页，使用小页的组合
                    Self::CACHE_WRITE_COMBINE
                } else {
                    // 对于非叶子页表项，不支持 Write Combine，退回到 Write Back
                    Self::CACHE_WRITE_BACK
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
pub struct X86PageEntry(pub(super) usize);
pub struct EntryPtr(pub(super) *const AtomicUsize);

impl EntryPtr {
    pub fn read(&self) -> X86PageEntry {
        X86PageEntry(unsafe { (*self.0).load(Ordering::Relaxed) })
    }

    pub fn write(&self, entry: X86PageEntry) {
        unsafe {
            (*self.0).store(entry.0, Ordering::Relaxed);
        }
    }
}

impl X86PageEntry {
    pub fn get_ptr(&self) -> *const usize {
        (self.0 & !0xFFF) as *const usize
    }

    pub(super) const fn new_mapped(frame: FrameNumber, flags: PageFlags, level: usize) -> Self {
        let mut entry = Self(frame.get() * ArchPageTable::PAGE_SIZE);

        entry.set_flags(flags, level);
        entry
    }

    pub(super) const fn set_flags(&mut self, flags: PageFlags, level: usize) {
        let flags = X86PageFlags::from(flags, level);
        self.0 = (self.0 & !0xFFF) | flags.0;
    }
}

impl PageTableEntry for X86PageEntry {
    fn new_absent() -> Self {
        X86PageEntry(0)
    }

    fn new_mapped(frame: FrameNumber, flags: PageFlags, page_level: usize) -> Self {
        X86PageEntry::new_mapped(frame, flags, page_level)
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

    fn set_flags(&mut self, flags: PageFlags, page_level: usize) {
        X86PageEntry::set_flags(self, flags, page_level);
    }
}

impl PageTableEntrySlot for EntryPtr {
    type Entry = X86PageEntry;

    fn read(&self) -> Self::Entry {
        EntryPtr::read(self)
    }

    fn write(&self, entry: Self::Entry) {
        EntryPtr::write(self, entry);
    }
}
