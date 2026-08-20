use core::ptr::NonNull;

use crate::{
    acpi::tables::{RsdpV1, TableManager},
    lib::rust::spinlock::{SpinGuard, Spinlock},
};

pub mod tables;

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
