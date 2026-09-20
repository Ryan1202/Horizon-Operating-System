//! 占位 IrqDescriptor

use super::{Domain, HardwareIrq, IrqChip, IrqData, IrqError, IrqNumber};
use crate::kernel::{interrupt::irq::RawIrq, memory::kmalloc::Kmalloc, topology::CpuMask};
use alloc::{boxed::Box, sync::Arc};
use core::any::Any;

static DOMAIN: Placeholder = Placeholder;

pub(super) struct Placeholder;

impl Placeholder {
    pub(super) fn data(irq: IrqNumber) -> IrqData {
        let number = RawIrq::new(irq.get() as u32);
        IrqData::new(
            HardwareIrq::<Self>::new(number),
            &DOMAIN,
            Arc::new_in(Self, Kmalloc::default()),
            Box::new_in((), Kmalloc::default()),
            None,
        )
    }
}

impl Domain for Placeholder {
    fn allocate(&self, _: IrqNumber, _: &dyn Any) -> Result<IrqData, IrqError> {
        Err(IrqError::Unsupported)
    }

    unsafe fn activate(&self, _: IrqNumber, _: &IrqData, _: &CpuMask) -> Result<CpuMask, IrqError> {
        Err(IrqError::Unsupported)
    }

    unsafe fn deactivate(&self, _: &IrqData) -> Result<(), IrqError> {
        unreachable!("placeholder IRQ cannot be active");
    }

    fn synchronize(&self, _: &IrqData) {
        unreachable!("placeholder cannot be activated");
    }

    unsafe fn update_affinity(
        &self,
        _: IrqNumber,
        _: &IrqData,
        _: &CpuMask,
    ) -> Result<CpuMask, IrqError> {
        Err(IrqError::Unsupported)
    }

    fn try_reclaim_route(&self, _: &IrqData) -> Result<(), IrqError> {
        Err(IrqError::Unsupported)
    }
}

impl IrqChip for Placeholder {
    fn mask(&self, _: &IrqData) {
        unreachable!("placeholder IRQ");
    }

    fn unmask(&self, _: &IrqData) {
        unreachable!("placeholder IRQ");
    }

    fn ack(&self, _: &IrqData) {
        unreachable!("placeholder IRQ");
    }

    fn eoi(&self, _: &IrqData) {
        unreachable!("placeholder IRQ");
    }
}
