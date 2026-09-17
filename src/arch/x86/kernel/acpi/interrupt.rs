use crate::{
    acpi::tables::madt,
    arch::x86::kernel::interrupt::apic::{ApicId, Gsi, IoApicInfo},
    kernel::interrupt::irq::RawIrq,
};

impl IoApicInfo {
    pub fn from_ioapic(ioapic: &madt::IoApic) -> Self {
        let id = ApicId::new(ioapic.ioapic_id as u32);
        let address = ioapic.ioapic_address as usize;
        let gsi_base = Gsi::new(RawIrq::new(ioapic.gsi_base));
        Self {
            id,
            address,
            gsi_base,
        }
    }
}
