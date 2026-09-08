use core::{any::Any, cell::SyncUnsafeCell, mem::MaybeUninit};

use alloc::{boxed::Box, sync::Arc};

use crate::{
    arch::x86::kernel::interrupt::{IRQ_RESERVATION, apic::Gsi},
    kernel::{
        interrupt::irq::{self, HardwareIrq, IrqChip, IrqData, IrqError, IrqNumber},
        memory::kmalloc::Kmalloc,
    },
    lib::rust::spinlock::Spinlock,
};

const IRQ_COUNT: usize = 16;

pub(super) static LEGACY_IRQ_DOMAIN: LegacyIrq = LegacyIrq {
    map: Spinlock::new([const { None }; IRQ_COUNT]),
};
pub(super) static LEGACY_IRQ_CHIP: SyncUnsafeCell<MaybeUninit<Arc<LegacyIrqChip, Kmalloc>>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

pub struct IrqOverride {
    gsi: Gsi,
    active_low: bool,
    level_triggered: bool,
}

impl IrqOverride {
    pub const fn new(gsi: Gsi, active_low: bool, level_triggered: bool) -> Self {
        Self {
            gsi,
            active_low,
            level_triggered,
        }
    }
}

/// 用于占位的 irq，不实现 8259a 支持
pub struct LegacyIrq {
    map: Spinlock<[Option<IrqOverride>; IRQ_COUNT]>,
}
pub(super) struct LegacyIrqChip;

impl LegacyIrq {
    pub fn get<'a>() -> &'a Self {
        &LEGACY_IRQ_DOMAIN
    }

    pub fn override_irq(&self, irq: usize, interrupt: IrqOverride) -> Option<()> {
        if irq < IRQ_COUNT {
            self.map.lock()[irq] = Some(interrupt);
            Some(())
        } else {
            None
        }
    }

    pub const fn irq(&self, irq: HardwareIrq<Self>) -> Option<&IrqOverride> {
        if (irq.get() as usize) < IRQ_COUNT {
            self.map.get_relaxed()[irq.get() as usize].as_ref()
        } else {
            None
        }
    }
}

impl irq::Domain for LegacyIrq {
    fn allocate(
        &self,
        irq: IrqNumber,
        data: &mut MaybeUninit<IrqData>,
        _any: &dyn Any,
    ) -> Result<(), IrqError> {
        let number = irq.get();
        let start = unsafe { (*IRQ_RESERVATION.get()).assume_init_ref() }
            .get(0)
            .unwrap()
            .get();

        let number = number
            .checked_sub(start)
            .ok_or(IrqError::InvalidIrqNumber(irq.get()))?;
        if number >= IRQ_COUNT {
            return Err(IrqError::InvalidIrqNumber(irq.get()));
        }

        let r#ref = &LEGACY_IRQ_DOMAIN;
        let chip = unsafe { (*LEGACY_IRQ_CHIP.get()).assume_init_ref() }.clone();
        let chip = data.write(IrqData::new(
            HardwareIrq::<Self>::new(number as u32),
            r#ref,
            chip,
            Box::new_in((), Kmalloc::default()),
            None,
        ));
        Ok(())
    }

    fn free(&self, _data: &IrqData) {}

    fn activate(
        &self,
        _irq: IrqNumber,
        _data: &IrqData,
        _affinity: &mut irq::Affinity,
    ) -> Result<(), irq::IrqError> {
        Ok(())
    }

    fn deactivate(&self, _data: &IrqData) {}
}

impl IrqChip for LegacyIrqChip {
    fn mask(&self, _irq: IrqNumber, _data: &IrqData) {}
    fn unmask(&self, _irq: IrqNumber, _data: &IrqData) {}

    fn ack(&self, _irq: IrqNumber, _data: &IrqData) {}
    fn eoi(&self, _irq: IrqNumber, _data: &IrqData) {}
}
