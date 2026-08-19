use core::ptr::NonNull;

use crate::{
    acpi::{
        TableCheck,
        tables::{DescriptionTable, TableHeader},
    },
    arch::PhysAddr,
};

const XSDT_SIGNATURE: &[u8; 4] = b"XSDT";
const RSDT_SIGNATURE: &[u8; 4] = b"RSDT";

#[repr(C, packed)]
pub struct Xsdt {
    header: TableHeader,
    entries: (),
}

impl DescriptionTable for Xsdt {
    const SIGN: &[u8; 4] = XSDT_SIGNATURE;
}

#[repr(C, packed)]
pub struct Rsdt {
    header: TableHeader,
    entries: (),
}

impl DescriptionTable for Rsdt {
    const SIGN: &[u8; 4] = RSDT_SIGNATURE;
}

impl Xsdt {
    pub fn from_ptr(ptr: NonNull<Xsdt>) -> Option<&'static Self> {
        let xsdt = unsafe { ptr.as_ref() };
        xsdt.check().then_some(xsdt)
    }

    pub fn iter_entries(&self) -> SdtEntryIter {
        let count = (self.header.length as usize - size_of::<Xsdt>()) / 8;
        SdtEntryIter::V2 {
            entries: (&raw const self.entries).cast(),
            index: 0,
            count,
        }
    }
}

impl Rsdt {
    pub fn from_rsdt(ptr: NonNull<Rsdt>) -> Option<&'static Self> {
        let rsdt = unsafe { ptr.as_ref() };
        rsdt.check().then_some(rsdt)
    }

    pub fn iter_entries(&self) -> SdtEntryIter {
        let count = (self.header.length as usize - size_of::<Rsdt>()) / 4;
        SdtEntryIter::V1 {
            entries: (&raw const self.entries).cast(),
            index: 0,
            count,
        }
    }
}

pub enum SdtEntryIter {
    V1 {
        entries: *const u32,
        index: usize,
        count: usize,
    },
    V2 {
        entries: *const u64,
        index: usize,
        count: usize,
    },
}

impl Iterator for SdtEntryIter {
    type Item = PhysAddr;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            &mut SdtEntryIter::V1 {
                entries,
                ref mut index,
                count,
            } => {
                if *index < count {
                    let addr =
                        PhysAddr::new(unsafe { entries.add(*index).read_unaligned() } as usize);
                    *index += 1;
                    Some(addr)
                } else {
                    None
                }
            }
            &mut SdtEntryIter::V2 {
                entries,
                ref mut index,
                count,
            } => {
                if *index < count {
                    let addr =
                        PhysAddr::new(unsafe { entries.add(*index).read_unaligned() } as usize);
                    *index += 1;
                    Some(addr)
                } else {
                    None
                }
            }
        }
    }
}
