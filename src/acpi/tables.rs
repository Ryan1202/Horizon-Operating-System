use core::{ptr::NonNull, slice};

mod fadt;
mod madt;
mod rsdp;
mod xsdt;

pub use rsdp::{RSDP_SIGNATURE, RsdpV1};

use crate::{
    acpi::{
        AcpiArchInterface,
        tables::{
            fadt::{FADT_SIGNATURE, Fadt, IapcBootCapabilities},
            madt::{MADT_SIGNATURE, Madt},
            rsdp::{RSDP, RsdpV2},
            xsdt::{Rsdt, Xsdt},
        },
    },
    arch::ArchAcpi,
};

#[allow(unused)]
#[repr(C, packed)]
pub struct TableHeader {
    signature: [u8; 4],
    length: u32,
    revision: u8,
    checksum: u8,
    oem_id: [u8; 6],
    oem_table_id: [u8; 8],
    oem_revision: u32,
    creator_id: u32,
    creator_revision: u32,
}

enum Entry {
    V1 {
        _rsdp: &'static RsdpV1,
        rsdt: &'static Rsdt,
    },
    V2 {
        _rsdp: &'static RsdpV2,
        xsdt: &'static Xsdt,
    },
}

pub trait TableCheck: Sized {
    fn check(&self) -> bool {
        let ptr = self as *const Self as *const u8;
        let size = size_of::<Self>();
        let sum: u8 = unsafe { slice::from_raw_parts(ptr, size) }
            .iter()
            .fold(0, |a, v| a.wrapping_add(*v));
        sum == 0
    }
}

pub trait DescriptionTable {
    const SIGN: &[u8; 4];
}

impl<T: DescriptionTable> TableCheck for T {
    fn check(&self) -> bool {
        let header = unsafe { (self as *const T as *const TableHeader).as_ref_unchecked() };

        if header.signature != *T::SIGN {
            return false;
        }

        let length = header.length as usize;
        let ptr = self as *const T as *const u8;
        let sum: u8 = unsafe { slice::from_raw_parts(ptr, length) }
            .iter()
            .fold(0, |a, v| a.wrapping_add(*v));
        sum == 0
    }
}

pub struct TableManager {
    entry: Entry,
    fadt: Option<&'static Fadt>,
    madt: Option<&'static Madt>,
}

impl TableManager {
    pub fn new() -> Option<Self> {
        let rsdp_v1 = RSDP
            .rsdp
            .check()
            .then_some(NonNull::from_ref(&RSDP.rsdp))
            .or_else(|| ArchAcpi::get_rsdp())?;

        let entry = RsdpV2::try_from_v1(rsdp_v1).map_or_else(
            |v1| {
                let ptr = unsafe { v1.as_ref() }
                    .get_rsdt_address()
                    .try_to_virt()?
                    .as_mut_ptr();
                let ptr = unsafe { NonNull::new_unchecked(ptr) };
                let rsdt = Rsdt::from_rsdt(ptr)?;
                Some(Entry::V1 {
                    _rsdp: unsafe { v1.as_ref() },
                    rsdt,
                })
            },
            |v2| {
                let ptr = unsafe { v2.as_ref() }
                    .get_xsdt_address()
                    .try_to_virt()?
                    .as_mut_ptr();
                let ptr = unsafe { NonNull::new_unchecked(ptr) };
                let xsdt = Xsdt::from_ptr(ptr)?;
                Some(Entry::V2 {
                    _rsdp: unsafe { v2.as_ref() },
                    xsdt,
                })
            },
        )?;

        Some(Self {
            entry,
            fadt: None,
            madt: None,
        })
    }

    pub(super) fn parse(&mut self) {
        let iter = match self.entry {
            Entry::V1 { rsdt, .. } => rsdt.iter_entries(),
            Entry::V2 { xsdt, .. } => xsdt.iter_entries(),
        };

        for entry in iter {
            let Some(ptr): Option<NonNull<[u8; 4]>> = entry
                .try_to_virt()
                .map(|v| unsafe { NonNull::new_unchecked(v.as_mut_ptr()) })
            else {
                continue;
            };

            let sign = unsafe { ptr.as_ref() };
            match sign {
                FADT_SIGNATURE => {
                    let fadt = unsafe { ptr.cast().as_ref() };
                    self.fadt = Fadt::check(fadt).then_some(fadt);
                }
                MADT_SIGNATURE => {
                    let madt = unsafe { ptr.cast().as_ref() };
                    self.madt = Madt::check(madt).then_some(madt);
                }
                _ => {}
            }
        }
    }

    #[cfg(target_arch = "x86_64")]
    pub const fn get_boot_capabilities(&self) -> Option<IapcBootCapabilities> {
        if let Some(fadt) = self.fadt {
            Some(fadt.get_iapc_capabilities())
        } else {
            None
        }
    }

    /// 获取本地中断控制器地址
    pub const fn get_local_ic_address(&self) -> Option<u32> {
        if let Some(madt) = self.madt {
            Some(madt.get_local_ic_address())
        } else {
            None
        }
    }

    pub const fn is_pic_pcat_compat(&self) -> bool {
        if let Some(madt) = self.madt {
            madt.is_pcat_compat()
        } else {
            false
        }
    }
}

#[repr(C, packed)]
pub struct GenericAddress {
    address_space_id: u8,
    register_bit_width: u8,
    register_bit_offset: u8,
    access_size: u8,
    address: u64,
}
