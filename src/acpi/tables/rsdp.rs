use core::ptr::NonNull;

use crate::{acpi::tables::TableCheck, arch::PhysAddr};

pub const RSDP_SIGNATURE: &[u8; 8] = b"RSD PTR ";

#[unsafe(no_mangle)]
pub static RSDP: RsdpV2 = RsdpV2 {
    rsdp: RsdpV1 {
        signature: [0; 8],
        checksum: 0,
        oem_id: [0; 6],
        revision: 2,
        rsdt_address: 0,
    },
    length: 36,
    xsdt_address: 0,
    extended_checksum: 0,
    reserved: [0; 3],
}; // 临时占位的数据，需要在启动时更新

#[repr(C, packed)]
pub struct RsdpV1 {
    signature: [u8; 8],
    checksum: u8,
    oem_id: [u8; 6],
    revision: u8,
    rsdt_address: u32,
}
impl TableCheck for RsdpV1 {}

impl RsdpV1 {
    pub fn get_rsdt_address(&self) -> PhysAddr {
        PhysAddr::new(self.rsdt_address as usize)
    }
}

#[repr(C, packed)]
pub struct RsdpV2 {
    pub rsdp: RsdpV1,
    length: u32,
    xsdt_address: u64,
    extended_checksum: u8,
    reserved: [u8; 3],
}
impl TableCheck for RsdpV2 {}

impl RsdpV2 {
    pub fn try_from_v1(v1: NonNull<RsdpV1>) -> Result<NonNull<RsdpV2>, NonNull<RsdpV1>> {
        let v1_ref = unsafe { v1.as_ref() };
        if v1_ref.check() && v1_ref.revision >= 2 {
            Ok(v1.cast())
        } else {
            Err(v1)
        }
    }

    pub fn get_xsdt_address(&self) -> PhysAddr {
        PhysAddr::new(self.xsdt_address as usize)
    }
}
