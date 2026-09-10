#[cfg(target_arch = "x86_64")]
pub mod x86 {
    pub mod kernel {
        pub mod acpi;
        pub mod interrupt;
        pub mod msr;
        pub mod page;
        pub mod percpu;
        pub mod thread;
    }

    pub mod drivers {
        pub mod interrupt {
            pub mod apic;
        }
    }
}

#[cfg(target_arch = "x86_64")]
pub use x86::kernel::{
    acpi::X86Acpi as ArchAcpi,
    interrupt::{X86Interrupt as ArchInterrupt, legacy::request_device_irq},
    page::{
        addr::{PhysAddr, VirtAddr},
        entry::X86EntryInfo as ArchPageEntry,
        table::X86PageTable as ArchPageTable,
        tlb::X86FlushTlb as ArchFlushTlb,
    },
    percpu::X86CpuLocal as ArchCpuLocal,
    thread::X86ThreadContext as ArchThreadContext,
};
