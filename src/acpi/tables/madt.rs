use core::ptr::{NonNull, read_unaligned};

use crate::acpi::tables::{DescriptionTable, TableHeader, madt::iso::InterruptSourceOverride};

mod ioapic;
mod iso;
mod lapic;

pub use ioapic::IoApic;
pub use lapic::{LocalApicFlags, ProcessorLocalApic};

pub const MADT_SIGNATURE: &[u8; 4] = b"APIC";

#[repr(C, packed)]
pub struct Madt {
    _header: TableHeader,
    local_ic_address: u32,
    flags: MadtFlags,
    entries: (),
}

impl Madt {
    pub const fn get_local_ic_address(&self) -> u32 {
        self.local_ic_address
    }

    pub const fn is_pcat_compat(&self) -> bool {
        unsafe { read_unaligned(&raw const self.flags) }.is_pcat_compat()
    }

    pub fn iter_entries(&self) -> MadtEntryIter {
        let start = NonNull::from_ref(&self.entries);
        let length = self._header.length as usize - size_of::<Madt>();
        let end = unsafe { start.byte_add(length) };
        MadtEntryIter { next: start, end }
    }
}

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct MadtFlags(u32);

impl MadtFlags {
    pub const fn is_pcat_compat(&self) -> bool {
        self.0 & 1 != 0
    }
}

impl DescriptionTable for Madt {
    const SIGN: &[u8; 4] = MADT_SIGNATURE;
}

pub enum InterruptController {
    LocalApic(&'static ProcessorLocalApic),
    IoApic(&'static IoApic),
    InterruptSourceOverride(&'static InterruptSourceOverride),
    NmiSource,
    LocalApicNmi,
    LocalApicOverride,
    IoSapic,
    LocalSapic,
    PlatformInterruptSource,
    LocalX2Apic,
    LocalX2ApicNmi,
    Gicc,
    Gicd,
    GicMsiFrame,
    Gicr,
    Its,
    MpWakeup,
    CorePic,
    LioPic,
    HtPic,
    EioPic,
    MsiPic,
    BioPic,
    LpcPic,
    Rintc,
    Imsic,
    Aplic,
    Plic,
    Unknown(NonNull<()>),
}

pub struct MadtEntryIter {
    next: NonNull<()>,
    end: NonNull<()>,
}

impl Iterator for MadtEntryIter {
    type Item = InterruptController;

    fn next(&mut self) -> Option<Self::Item> {
        if self.next >= self.end {
            return None;
        }

        let current = self.next;
        let head = unsafe { current.cast::<[u8; 2]>().as_ref() };
        let _type = head[0];
        let length = head[1] as usize;
        self.next = unsafe { current.byte_add(length) };

        Some(match _type {
            0 => InterruptController::LocalApic(unsafe { current.cast().as_ref() }),
            1 => InterruptController::IoApic(unsafe { current.cast().as_ref() }),
            2 => InterruptController::InterruptSourceOverride(unsafe { current.cast().as_ref() }),
            _ => InterruptController::Unknown(current),
        })
    }
}
