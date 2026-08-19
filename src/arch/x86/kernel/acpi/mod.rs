use core::{cell::SyncUnsafeCell, ops::Range, ptr::NonNull};

use crate::{
    acpi::{AcpiArchInterface, RSDP_SIGNATURE, RsdpV1, TableCheck, acpi},
    arch::PhysAddr,
};

pub struct X86Acpi;

const RSDP_RANGE: [Range<usize>; 2] = [0x009FC000..0x00A00000, 0x000E0000..0x00100000];

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
        if let Some(cap) = acpi.tables().get_boot_capabilities() {
            boot_capabilities.i8042 &= cap.i8042 as u8;
            boot_capabilities.vga &= cap.vga as u8;
            boot_capabilities.msi &= cap.msi as u8;
            boot_capabilities.pcie_aspm &= cap.pcie_aspm as u8;
            boot_capabilities.rtc &= cap.rtc as u8;
        }

        if let Some(addr) = acpi.tables().get_local_ic_address() {
            boot_capabilities.lapic_address = addr;
        }

        if let Some(has_pic) = acpi.tables().is_pic_pcat_compat().then_some(true) {
            boot_capabilities.pic = 1;
        }
    }
}
