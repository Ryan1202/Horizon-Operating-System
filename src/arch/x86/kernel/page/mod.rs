use crate::{
    arch::x86::kernel::page::{entry::X86PageEntry, table::X86PageTable},
    kernel::memory::arch::ArchMemory,
};

pub mod addr;
pub mod entry;
pub mod table;
pub mod tlb;

impl ArchMemory for X86PageTable {
    type Entry = X86PageEntry;

    const PAGE_BITS: usize = 12;
    const PAGE_TABLE_LEVELS: usize = 2;
    const PHYS_ADDR_BITS: usize = 32;
    const VIRT_ADDR_BITS: usize = 32;
}
