use crate::acpi::tables::{DescriptionTable, TableHeader};

pub const MADT_SIGNATURE: &[u8; 4] = b"APIC";

pub struct Madt {
    header: TableHeader,
    local_ic_address: u32,
    flags: MadtFlags,
}

impl Madt {
    pub const fn get_local_ic_address(&self) -> u32 {
        self.local_ic_address
    }

    pub const fn is_pcat_compat(&self) -> bool {
        self.flags.is_pcat_compat()
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
