mod ioapic;
mod lapic;

pub use ioapic::IoApics;
pub use lapic::{LocalXApic, LvtEntry};
