use crate::{arch::x86::kernel::page::table::X86PageTable, kernel::memory::arch::ArchMemory};

pub mod addr;
pub mod entry;
pub mod table;
pub mod tlb;

impl ArchMemory for X86PageTable {
    const PAGE_BITS: usize = 12;
    const PHYS_ADDR_BITS: usize = 52;
    const VIRT_ADDR_BITS: usize = 48;
}
