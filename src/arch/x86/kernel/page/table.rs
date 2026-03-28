use core::sync::atomic::{AtomicU32, Ordering};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr, x86::kernel::page::entry::X86PageEntry},
    kernel::memory::{
        MemoryError,
        arch::ArchMemory,
        frame::{
            buddy::FrameOrder, options::FrameAllocOptions, reference::UniqueFrames, zone::ZoneType,
        },
        page::{
            PageEntry, PageFlags, PageTableError, PageTableWalker, dyn_pages::DynPages,
            options::PageAllocOptions,
        },
    },
};

pub const PDE_BASE: usize = 0xFFFF_F000;
pub const PTE_BASE: usize = 0xFFC0_0000;

pub const PDE_SHIFT: usize = 10;
pub const PAGES_PER_HUGE_PAGE: usize = 1024;

pub struct X86PageTable;

impl X86PageTable {
    pub fn read_pde_entry(page_number: usize) -> X86PageEntry {
        let pde_addr = (PDE_BASE + (page_number >> PDE_SHIFT) * 4) as *const AtomicU32;
        X86PageEntry(unsafe { (*pde_addr).load(Ordering::Relaxed) })
    }

    pub fn write_pde_entry(page_number: usize, entry: X86PageEntry) {
        let pde_addr = (PDE_BASE + (page_number >> PDE_SHIFT) * 4) as *mut AtomicU32;
        unsafe {
            (*pde_addr).store(entry.0, Ordering::Relaxed);
        };
    }

    pub fn read_pte_entry(page_number: usize) -> X86PageEntry {
        let pte_addr = (PTE_BASE + page_number * 4) as *const AtomicU32;
        X86PageEntry(unsafe { (*pte_addr).load(Ordering::Relaxed) })
    }

    pub fn write_pte_entry(page_number: usize, entry: X86PageEntry) {
        let pte_addr = (PTE_BASE + page_number * 4) as *mut AtomicU32;
        unsafe {
            (*pte_addr).store(entry.0, Ordering::Relaxed);
        };
    }

    fn alloc_table_frame(flags: PageFlags) -> Result<X86PageEntry, MemoryError> {
        let frame_options = FrameAllocOptions::new().fallback(&[ZoneType::LinearMem]);
        let page_options = PageAllocOptions::new(frame_options)
            .contiguous(true)
            .zeroed(true);

        let mut page = page_options.allocate()?;

        let frame = page.get_frame().unwrap();

        let pde_flags = if flags.huge_page {
            flags
        } else {
            PageFlags::new()
        };
        let new_pde = X86PageEntry::new_mapped(frame.to_frame_number(), pde_flags, 1);

        Ok(new_pde)
    }
}

impl PageTableWalker for X86PageTable {
    type Entry = X86PageEntry;

    fn map(
        pages: &mut DynPages,
        offset: usize,
        frame: &mut UniqueFrames,
        flags: PageFlags,
    ) -> Result<(), MemoryError> {
        let page_number = pages.start_addr().to_page_number().unwrap().get().get() + offset;

        // 尝试使用大页
        if frame.get_order().get() >= 10 && FrameOrder::from_count(page_number).get() >= 10 {
            let pde = Self::read_pde_entry(page_number);
            if pde.is_present() {
                return Err(PageTableError::EntryAlreadyMapped.into());
            }

            Self::write_pde_entry(
                page_number,
                X86PageEntry::new_mapped(frame.to_frame_number(), flags, 1),
            );
            return Ok(());
        }

        let frame_number = frame.to_frame_number();
        for i in 0..frame.get_order().to_count().get() {
            let page_number = page_number + i;

            let pde = Self::read_pde_entry(page_number);
            if !pde.is_present() {
                // 需要分配新的页表
                let new_pde = Self::alloc_table_frame(flags)?;

                Self::write_pde_entry(page_number, new_pde);
            }

            let pte = Self::read_pte_entry(page_number);
            if pte.is_present() {
                return Err(PageTableError::EntryAlreadyMapped.into());
            }

            let new_pte = X86PageEntry::new_mapped(frame_number + i, flags, 0);
            Self::write_pte_entry(page_number, new_pte);
        }

        Ok(())
    }

    fn translate(vaddr: VirtAddr) -> Result<PhysAddr, PageTableError> {
        let page_number = vaddr.to_page_number().unwrap().get().get();

        let pde_entry = Self::read_pde_entry(page_number);
        if !pde_entry.is_present() {
            return Err(PageTableError::EntryNotPresent);
        } else if pde_entry.is_huge(1) {
            return Ok(PhysAddr::new(
                (pde_entry.0 as usize & 0xFFC0_0000) + (vaddr.as_usize() & 0x003F_FFFF),
            ));
        }

        let pte_entry = Self::read_pte_entry(page_number);
        if !pte_entry.is_present() {
            return Err(PageTableError::EntryNotPresent);
        }

        let phys_addr = PhysAddr::new((pte_entry.0 & 0xFFFF_F000) as usize | vaddr.page_offset());
        Ok(phys_addr)
    }

    fn unmap(pages: &mut DynPages, offset: usize, order: FrameOrder) -> Result<(), PageTableError> {
        let vaddr = pages.start_addr() + offset * ArchPageTable::PAGE_SIZE;
        let page_number = vaddr.to_page_number().unwrap().get().get();

        let pde = Self::read_pde_entry(page_number);
        if !pde.is_present() {
            return Err(PageTableError::EntryNotPresent);
        } else if pde.is_huge(1) {
            if order.get() >= 10 {
                let entry = X86PageEntry::new_absent();
                Self::write_pde_entry(page_number, entry);
            } else {
                return Err(PageTableError::InvalidLevel);
            }
        }

        for i in 0..order.to_count().get() {
            let pte = Self::read_pte_entry(page_number + i);
            if !pte.is_present() {
                return Err(PageTableError::EntryNotPresent);
            }

            let entry = X86PageEntry::new_absent();
            Self::write_pte_entry(page_number, entry);
        }

        Ok(())
    }

    fn update_flags(pages: &mut DynPages, flags: PageFlags) -> Result<(), PageTableError> {
        let pde = Self::read_pde_entry(pages.start_addr().to_page_number().unwrap().get().get());
        if !pde.is_present() {
            return Err(PageTableError::EntryNotPresent);
        }

        let mut pte =
            Self::read_pte_entry(pages.start_addr().to_page_number().unwrap().get().get());
        if !pte.is_present() {
            return Err(PageTableError::EntryNotPresent);
        }

        pte.set_flags(flags);
        Self::write_pte_entry(
            pages.start_addr().to_page_number().unwrap().get().get(),
            pte,
        );
        Ok(())
    }
}
