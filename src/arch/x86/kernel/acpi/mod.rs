use core::{
    cell::SyncUnsafeCell,
    mem::MaybeUninit,
    ops::Range,
    ptr::{NonNull, read_unaligned},
};

use alloc::boxed::Box;

use crate::{
    acpi::{
        AcpiArchInterface, acpi,
        tables::{RSDP_SIGNATURE, RsdpV1, TableCheck, madt::InterruptController},
    },
    arch::{
        PhysAddr,
        x86::kernel::acpi::{
            cpu::Cpu,
            interrupt::{ApicId, Gsi, IoApic, IrqOverride, IrqRouting},
        },
    },
    kernel::{
        memory::kmalloc::Kmalloc,
        topology::{CpuHardwareId, CpuRegistry},
    },
};

mod cpu;
mod interrupt;

unsafe extern "C" {
    fn get_local_cpu_id() -> i32;
}

const RSDP_RANGE: [Range<usize>; 2] = [0x009FC000..0x00A00000, 0x000E0000..0x00100000];

static X86_TOPOLOGY: SyncUnsafeCell<X86Topology> = SyncUnsafeCell::new(X86Topology {
    cpus: MaybeUninit::uninit(),
    io_apic: MaybeUninit::uninit(),
    irq_routing: None,
});

pub struct X86Acpi;

pub struct X86Topology {
    cpus: MaybeUninit<Box<[Cpu], Kmalloc>>,
    io_apic: MaybeUninit<Box<[IoApic], Kmalloc>>,
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
            let (lapic_count, ioapic_count) = madt.iter_entries().fold(
                (0, 0),
                |(lapic_count, ioapic_count), entry| match entry {
                    InterruptController::LocalApic(_) => (lapic_count + 1, ioapic_count),
                    InterruptController::IoApic(_) => (lapic_count, ioapic_count + 1),
                    _ => (lapic_count, ioapic_count),
                },
            );

            let mut cpus = Box::new_uninit_slice_in(lapic_count, Kmalloc::default());
            let mut ioapics = Box::new_uninit_slice_in(ioapic_count, Kmalloc::default());

            let mut cpu_index = 0;
            let mut ioapic_index = 0;
            for entry in madt.iter_entries() {
                match entry {
                    InterruptController::LocalApic(local_apic) => {
                        let cpu = Cpu::from_local_apic(local_apic);
                        cpus[cpu_index].write(cpu);
                        cpu_index += 1;
                    }
                    InterruptController::IoApic(io_apic) => {
                        ioapics[ioapic_index].write(IoApic::from_ioapic(io_apic));
                        ioapic_index += 1;
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
            topology.irq_routing = Some(irq_routing);

            let cpus = unsafe { cpus.assume_init() };
            let ioapics = unsafe { ioapics.assume_init() };

            topology.cpus = MaybeUninit::new(cpus);
            topology.io_apic = MaybeUninit::new(ioapics);
        }
    }

    #[unsafe(export_name = "acpi_register_cpus")]
    pub fn register_cpus() {
        let topology = unsafe { &mut *X86_TOPOLOGY.get() };
        let cpus = unsafe { topology.cpus.assume_init_mut() };

        let bsp_id = unsafe { u32::try_from(get_local_cpu_id()).expect("BSP CPU ID is invalid") };
        let bsp_id = ApicId::new(bsp_id);
        CpuRegistry::register(cpus, bsp_id.into(), |cpu| cpu.id().into());
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

    fn io_in_u8(port: u16) -> u8 {
        let value: u8;
        unsafe {
            core::arch::asm!("in al, dx", in("dx") port, out("al") value, options(nomem, nostack));
        }
        value
    }

    fn io_in_u16(port: u16) -> u16 {
        let value: u16;
        unsafe {
            core::arch::asm!("in ax, dx", in("dx") port, out("ax") value, options(nomem, nostack));
        }
        value
    }

    fn io_in_u32(port: u16) -> u32 {
        let value: u32;
        unsafe {
            core::arch::asm!("in eax, dx", in("dx") port, out("eax") value, options(nomem, nostack));
        }
        value
    }

    fn io_out_u8(port: u16, value: u8) {
        unsafe {
            core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack));
        }
    }

    fn io_out_u16(port: u16, value: u16) {
        unsafe {
            core::arch::asm!("out dx, ax", in("dx") port, in("ax") value, options(nomem, nostack));
        }
    }

    fn io_out_u32(port: u16, value: u32) {
        unsafe {
            core::arch::asm!("out dx, eax", in("dx") port, in("eax") value, options(nomem, nostack));
        }
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
    unsafe { topology.io_apic.assume_init_ref() }.len()
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
    let io_apic = unsafe { topology.io_apic.assume_init_ref() };

    if io_apic.is_empty() || index >= io_apic.len() {
        return -1;
    }

    let io_apic = &io_apic[index];
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
