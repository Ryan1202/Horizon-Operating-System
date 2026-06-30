use core::ops::Deref;

use crate::{
    arch::PhysAddr,
    kernel::memory::{MemoryError, dma::DmaAddr},
};

mod identity;

pub use identity::IDENTITY_BACKEND;

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Backend(&'static dyn Mapping);

impl Backend {
    pub const fn new(mapping: &'static dyn Mapping) -> Self {
        Self(mapping)
    }
}

impl Deref for Backend {
    type Target = dyn Mapping;

    fn deref(&self) -> &Self::Target {
        self.0
    }
}

pub trait Mapping: Sync {
    fn map(&self, phys_addr: PhysAddr, size: usize) -> Result<DmaAddr, MemoryError>;
    fn unmap(&self, dma_addr: DmaAddr) -> Result<(), MemoryError>;

    fn prepare_for_device(&self, dma_addr: DmaAddr, size: usize) -> Result<(), MemoryError>;
    fn prepare_for_cpu(&self, dma_addr: DmaAddr, size: usize) -> Result<(), MemoryError>;
}
