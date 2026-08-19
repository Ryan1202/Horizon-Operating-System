use core::ptr::NonNull;

use crate::lib::rust::spinlock::{SpinGuard, Spinlock};

mod tables;

pub use tables::{RSDP_SIGNATURE, RsdpV1, TableCheck, TableManager};

static ACPI: Spinlock<Option<Acpi>> = Spinlock::new(None);

pub trait AcpiArchInterface {
    fn get_rsdp() -> Option<NonNull<RsdpV1>>;
}

pub struct Acpi {
    table_manager: TableManager,
}

impl Acpi {
    pub fn new() -> Option<Self> {
        let mut table_manager = TableManager::new()?;
        table_manager.parse();

        Some(Self { table_manager })
    }

    pub const fn tables(&self) -> &TableManager {
        &self.table_manager
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn acpi_init() {
    *acpi() = Acpi::new();
}

pub fn acpi<'a>() -> SpinGuard<'a, &'a mut Option<Acpi>> {
    ACPI.lock()
}
