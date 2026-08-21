use core::ptr::read_unaligned;

use crate::{acpi::tables::madt::ProcessorLocalApic, arch::x86::kernel::acpi::interrupt::ApicId};

pub enum CpuState {
    Online,
    Offline,
    Unusable,
}

pub struct Cpu {
    id: ApicId,
    _state: CpuState,
}

impl Cpu {
    pub fn from_local_apic(local_apic: &ProcessorLocalApic) -> Self {
        let id = ApicId::new(local_apic.apic_id as u32);

        let flags = unsafe { read_unaligned(&raw const local_apic.flags) };
        let state = if flags.enabled() {
            CpuState::Online
        } else if flags.online_capable() {
            CpuState::Offline
        } else {
            CpuState::Unusable
        };
        Self { id, _state: state }
    }

    pub const fn id(&self) -> ApicId {
        self.id
    }

    pub const fn _state(&self) -> &CpuState {
        &self._state
    }
}
