use core::slice;

use crate::acpi::tables::{DescriptionTable, TableHeader};

const DSDT_SIGNATURE: &[u8; 4] = b"DSDT";

pub struct Dsdt {
    header: TableHeader,
    definition_block: (),
}

impl DescriptionTable for Dsdt {
    const SIGN: &'static [u8; 4] = DSDT_SIGNATURE;
}

impl Dsdt {
    pub const fn aml_bytes(&self) -> &[u8] {
        let size = self.header.length as usize - size_of::<TableHeader>();
        unsafe { slice::from_raw_parts(&raw const self.definition_block as *const u8, size) }
    }
}
