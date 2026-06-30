use crate::{
    arch::PhysAddr,
    kernel::memory::{
        MemoryError,
        dma::{
            DmaAddr,
            mapping::{Backend, Mapping},
        },
    },
};

pub struct IdentityMapping;

static IDENTITY_MAPPING: IdentityMapping = IdentityMapping;

#[unsafe(export_name = "dma_backend_identity")]
pub static IDENTITY_BACKEND: Backend = Backend::new(&IDENTITY_MAPPING);

impl Mapping for IdentityMapping {
    fn map(&self, paddr: PhysAddr, _size: usize) -> Result<DmaAddr, MemoryError> {
        Ok(DmaAddr::new(paddr.as_usize()))
    }

    fn unmap(&self, _dma_addr: DmaAddr) -> Result<(), MemoryError> {
        Ok(())
    }

    fn prepare_for_cpu(&self, _dma_addr: DmaAddr, _size: usize) -> Result<(), MemoryError> {
        Ok(())
    }

    fn prepare_for_device(&self, _dma_addr: DmaAddr, _size: usize) -> Result<(), MemoryError> {
        Ok(())
    }
}
