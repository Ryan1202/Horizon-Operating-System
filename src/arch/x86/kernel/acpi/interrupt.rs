use core::array;

use crate::acpi::tables::madt;

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct ApicId(u32);

impl ApicId {
    pub const fn new(id: u32) -> Self {
        Self(id)
    }

    pub const fn get(&self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct Gsi(pub u32);

pub struct IoApic {
    pub id: ApicId,
    pub address: usize,
    pub global_system_interrupt_base: Gsi,
}

impl IoApic {
    pub fn from_ioapic(ioapic: &madt::IoApic) -> Self {
        let id = ApicId::new(ioapic.ioapic_id as u32);
        let address = ioapic.ioapic_address as usize;
        let global_system_interrupt_base = Gsi(ioapic.global_system_interrupt_base);
        Self {
            id,
            address,
            global_system_interrupt_base,
        }
    }
}

const ISA_IRQ_COUNT: usize = 16;

#[derive(Clone, Copy)]
#[repr(C)]
pub struct IrqOverride {
    gsi: Gsi,
    active_low: u8,
    level_triggered: u8,
}

pub struct IrqRouting {
    isa: [IrqOverride; ISA_IRQ_COUNT],
}

impl IrqRouting {
    pub fn new() -> Self {
        Self {
            isa: array::from_fn(|irq| IrqOverride {
                gsi: Gsi(irq as u32),
                active_low: 0,
                level_triggered: 0,
            }),
        }
    }

    pub fn override_irq(&mut self, irq: usize, gsi: Gsi, active_low: bool, level_triggered: bool) {
        if irq < ISA_IRQ_COUNT {
            self.isa[irq] = IrqOverride {
                gsi,
                active_low: active_low as u8,
                level_triggered: level_triggered as u8,
            };
        }
    }

    pub const fn irq(&self, irq: usize) -> Option<&IrqOverride> {
        if irq < ISA_IRQ_COUNT {
            Some(&self.isa[irq])
        } else {
            None
        }
    }
}
