use core::ptr::NonNull;

use crate::{
    acpi::{
        aml::namespace::{NameSpace, init_namespace},
        tables::{RsdpV1, TableManager},
    },
    lib::rust::spinlock::{SpinGuard, Spinlock},
};

pub mod aml;
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

        init_namespace();

        let dsdt = table_manager.dsdt()?;
        let bytecode = aml::Bytecode::from_bytes(dsdt.aml_bytes());

        let guard = NameSpace::root().lock_pinned();
        let mut parser = aml::Parser::new(bytecode, guard.as_ref().get_ref());
        let _ = parser.parse();
        guard.as_ref().get_ref().print_tree();

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
