use core::ptr::NonNull;

use crate::{
    arch::{PhysAddr, VirtAddr},
    kernel::memory::{
        MemoryError::{self, InvalidPhysicalAddress},
        dma::{
            device::Device,
            source::{pool::Pool, software_iotlb::BounceAddr},
        },
    },
};

pub mod coherent;
pub(super) mod direct;
pub mod pool;
pub mod software_iotlb;

pub use software_iotlb::{BouncePool, SlotMeta};

pub enum Source {
    Coherent { size: usize },
    Pool { pool: NonNull<Pool> },
    Direct { paddr: PhysAddr, size: usize },
    SoftwareIotlb { paddr: PhysAddr, size: usize },
}

impl Source {
    pub fn acquire(
        &self,
        device: &mut Device,
    ) -> Result<(Option<VirtAddr>, PhysAddr), MemoryError> {
        match self {
            &Self::Coherent { size } => coherent::allocate(device, size).map(|(v, p)| (Some(v), p)),
            Self::Pool { pool } => unsafe { pool.as_ref().allocate() }.map(|(v, p)| (Some(v), p)),
            &Self::Direct { paddr, .. } => Ok((None, paddr)),
            &Self::SoftwareIotlb { paddr, size } => {
                software_iotlb::acquire(paddr, size, device).map(|(p, v)| (Some(v), p))
            }
        }
    }

    pub fn release(
        &self,
        device: &Device,
        vaddr: VirtAddr,
        paddr: PhysAddr,
    ) -> Result<(), MemoryError> {
        match self {
            Self::Coherent { .. } => coherent::deallocate(device, vaddr),
            Self::Pool { pool } => {
                unsafe { pool.as_ref() }.deallocate(NonNull::new(vaddr.as_mut_ptr::<u8>()).unwrap())
            }
            Self::Direct { .. } => Ok(()),
            Self::SoftwareIotlb { .. } => {
                BouncePool::get_pool()
                    .deallocate(BounceAddr::new(paddr).ok_or(InvalidPhysicalAddress(paddr))?);
                Ok(())
            }
        }
    }
}
