#[cfg(target_arch = "x86")]
pub mod x86 {
    pub mod kernel {
        pub mod page;
    }
}

#[cfg(target_arch = "x86")]
pub use x86::kernel::page::{
    addr::{PhysAddr, VirtAddr},
    entry::X86PageEntry as ArchPageEntry,
    table::X86PageTable as ArchPageTable,
    tlb::X86FlushTlb as ArchFlushTlb,
};
