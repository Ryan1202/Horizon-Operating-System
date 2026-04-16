use crate::{
    arch::ArchPageTable,
    kernel::memory::{
        arch::ArchMemory,
        page::{FlushTlb, PageNumber},
    },
};
use core::arch::asm;

pub struct X86FlushTlb;

impl FlushTlb for X86FlushTlb {
    fn flush_page(page_number: PageNumber) {
        unsafe {
            asm!(
                "invlpg [{}]",
                in(reg) page_number.get() * ArchPageTable::PAGE_SIZE,
                options(nostack, preserves_flags)
            );
        }
    }

    fn flush_range(start: PageNumber, end: PageNumber) {
        let count = end.get() - start.get();
        for i in 0..=count {
            Self::flush_page(start + i);
        }
    }

    fn flush_all() {
        unsafe {
            asm!(
                "mov {0}, cr3",
                "mov cr3, {0}",
                lateout(reg) _,
                options(nostack, preserves_flags)
            );
        }
    }

    fn flush_all_inclusive_global() {
        Self::flush_all();
    }
}
