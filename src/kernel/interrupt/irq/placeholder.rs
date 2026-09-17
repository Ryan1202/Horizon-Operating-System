//! 占位 IrqDescriptor

use super::{Affinity, Domain, HardwareIrq, IrqChip, IrqData, IrqError, IrqNumber};
use crate::kernel::{interrupt::irq::RawIrq, memory::kmalloc::Kmalloc};
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

    fn free(&self, _: &IrqData) {}

    fn activate(&self, _: IrqNumber, _: &IrqData, _: &mut Affinity) -> Result<(), IrqError> {
        Err(IrqError::Unsupported)
    }

    fn synchronize(&self, _: &IrqData) {
        unreachable!("placeholder cannot be activated");
    }

    fn deactivate(&self, _: &IrqData) {
        unreachable!("placeholder IRQ cannot be active");
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
