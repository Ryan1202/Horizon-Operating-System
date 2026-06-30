use crate::kernel::memory::{
    MemoryError,
    dma::{
        Direction,
        scatter_gather::EntryList,
        source::{SlotMeta, software_iotlb::BounceAddr},
        sync::{bounce::BounceSync, scatter_gather::ScatterGatherSync},
    },
};

pub mod bounce;
mod scatter_gather;

pub enum Sync {
    None,
    Bounce(BounceAddr, SlotMeta, usize),
}

impl Sync {
    pub fn sync_for_device(&mut self) -> Result<(), MemoryError> {
        match self {
            Self::None => Ok(()),
            Self::Bounce(addr, slot_meta, size) => {
                BounceSync::sync(*addr, slot_meta, *size, Direction::ToDevice)
            }
        }
    }

    pub fn sync_for_cpu(&mut self) -> Result<(), MemoryError> {
        match self {
            Self::None => Ok(()),
            Self::Bounce(phys_addr, slot_meta, size) => {
                BounceSync::sync(*phys_addr, slot_meta, *size, Direction::FromDevice)
            }
        }
    }
}

pub fn sync_sg_for_device(entries: &mut EntryList, n_entries: usize) -> Result<(), MemoryError> {
    ScatterGatherSync::sync(entries, n_entries, Direction::ToDevice)
}

pub fn sync_sg_for_cpu(entries: &mut EntryList, n_entries: usize) -> Result<(), MemoryError> {
    ScatterGatherSync::sync(entries, n_entries, Direction::FromDevice)
}
