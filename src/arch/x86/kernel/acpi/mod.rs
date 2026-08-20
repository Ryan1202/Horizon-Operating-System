use core::{
    cell::SyncUnsafeCell,
    ops::Range,
    ptr::{NonNull, read_unaligned},
};

use alloc::vec::Vec;

use crate::{
    acpi::{
        AcpiArchInterface, acpi,
        tables::{RSDP_SIGNATURE, RsdpV1, TableCheck, madt::InterruptController},
    },
    arch::{
        PhysAddr,
        x86::kernel::acpi::{
            cpu::Cpu,
            interrupt::{Gsi, IoApic, IrqOverride, IrqRouting},
        },
    },
    kernel::memory::kmalloc::Kmalloc,
};

mod cpu;
mod interrupt;

const RSDP_RANGE: [Range<usize>; 2] = [0x009FC000..0x00A00000, 0x000E0000..0x00100000];

static X86_TOPOLOGY: SyncUnsafeCell<X86Topology> = SyncUnsafeCell::new(X86Topology {
    cpus: Vec::new_in(Kmalloc::default()),
    io_apic: Vec::new_in(Kmalloc::default()),
    irq_routing: None,
});

pub struct X86Acpi;

pub struct X86Topology {
    cpus: Vec<Cpu, Kmalloc>,
    io_apic: Vec<IoApic, Kmalloc>,
    irq_routing: Option<IrqRouting>,
}

impl X86Topology {
    pub fn get() -> &'static Self {
        unsafe { &*X86_TOPOLOGY.get() }
    }

    #[unsafe(export_name = "acpi_init_x86_topology")]
    pub fn init() {
        let topology = unsafe { &mut *X86_TOPOLOGY.get() };
        let mut irq_routing = IrqRouting::new();

        if let Some(acpi) = acpi().as_ref()
            && let Some(madt) = acpi.tables().madt()
        {
            for entry in madt.iter_entries() {
                match entry {
                    InterruptController::LocalApic(local_apic) => {
                        topology.cpus.push(Cpu::from_local_apic(local_apic));
                    }
                    InterruptController::IoApic(io_apic) => {
                        topology.io_apic.push(IoApic::from_ioapic(io_apic));
                    }
                    InterruptController::InterruptSourceOverride(irq_override) => {
                        let irq = irq_override.source as usize;
                        let gsi = Gsi(irq_override.gsi);
                        let flags = unsafe { read_unaligned(&raw const irq_override.flags) };
                        let active_low = flags.active_low().unwrap_or(false);
                        let level_triggered = flags.level_triggered().unwrap_or(false);

                        irq_routing.override_irq(irq, gsi, active_low, level_triggered);
                    }
                    _ => {}
                }
            }
        }
        topology.irq_routing = Some(irq_routing);
    }
}

#[repr(C)]
pub struct BootCapabilities {
    pub i8042: u8,
    pub vga: u8,
    pub msi: u8,
    pub pcie_aspm: u8,
    pub rtc: u8,
    pub pic: u8,
    pub lapic_address: u32,
}

#[unsafe(export_name = "x86_boot_capabilities")]
static BOOT_CAPABILITIES: SyncUnsafeCell<BootCapabilities> =
    SyncUnsafeCell::new(BootCapabilities {
        i8042: 1,
        vga: 0,
        msi: 1,
        pcie_aspm: 1,
        rtc: 1,
        pic: 1,
        lapic_address: 0xFEE00000,
    });

impl AcpiArchInterface for X86Acpi {
    fn get_rsdp() -> Option<NonNull<RsdpV1>> {
        for range in RSDP_RANGE {
            let Some(mut addr) = PhysAddr::new(range.start).try_to_virt() else {
                continue;
            };
            let size = range.end - range.start;
            let end = addr + size;

            while addr < end {
                let rsdp_ptr = addr.as_mut_ptr::<[u8; 8]>();
                unsafe {
                    if *rsdp_ptr == *RSDP_SIGNATURE && RsdpV1::check(&*rsdp_ptr.cast()) {
                        return Some(NonNull::new_unchecked(rsdp_ptr).cast());
                    }
                }
                addr += 16;
            }
        }
        None
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn acpi_update_boot_capabilities() {
    let acpi = acpi();
    if let Some(acpi) = acpi.as_ref() {
        let boot_capabilities = unsafe { &mut *BOOT_CAPABILITIES.get() };
        if let Some(fadt) = acpi.tables().fadt() {
            let cap = fadt.get_iapc_capabilities();
            boot_capabilities.i8042 &= cap.i8042 as u8;
            boot_capabilities.vga &= cap.vga as u8;
            boot_capabilities.msi &= cap.msi as u8;
            boot_capabilities.pcie_aspm &= cap.pcie_aspm as u8;
            boot_capabilities.rtc &= cap.rtc as u8;
        }

        if let Some(madt) = acpi.tables().madt() {
            boot_capabilities.lapic_address = madt.get_local_ic_address();
            boot_capabilities.pic = madt.is_pcat_compat() as u8;
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn acpi_get_ioapic_count() -> usize {
    let topology = X86Topology::get();
    topology.io_apic.len()
}

#[repr(C)]
pub struct X86IoApic {
    id: u32,
    address: u32,
    gsi: u32,
}

#[unsafe(no_mangle)]
pub extern "C" fn acpi_get_ioapic_info(index: usize, out: *mut X86IoApic) -> i32 {
    let topology = X86Topology::get();

    if topology.io_apic.is_empty() || index >= topology.io_apic.len() {
        return -1;
    }

    let io_apic = &topology.io_apic[index];
    unsafe {
        *out = X86IoApic {
            id: io_apic.id.get(),
            address: io_apic.address as u32,
            gsi: io_apic.global_system_interrupt_base.0,
        }
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_acpi_get_isa_irq_route(irq: u32, out: *mut IrqOverride) -> i32 {
    let Some(out) = NonNull::new(out) else {
        return 0;
    };
    let Some(irq_override) = X86Topology::get()
        .irq_routing
        .as_ref()
        .and_then(|routing| routing.irq(irq as usize))
    else {
        return -1;
    };

    unsafe {
        out.write(*irq_override);
    }
    0
}
