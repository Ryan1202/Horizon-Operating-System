#[cfg(target_arch = "x86_64")]
pub mod x86 {
    pub mod kernel {
        pub mod interrupt;
        pub mod page;
        pub mod thread;
    }
}

#[cfg(target_arch = "x86_64")]
pub use x86::kernel::{
    interrupt::X86Interrupt as ArchInterrupt,
    page::{
        addr::{PhysAddr, VirtAddr},
        entry::X86EntryInfo as ArchPageEntry,
        table::X86PageTable as ArchPageTable,
        tlb::X86FlushTlb as ArchFlushTlb,
    },
    thread::X86ThreadContext as ArchThreadContext,
};
