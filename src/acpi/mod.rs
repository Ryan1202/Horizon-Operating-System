use core::{ops::ControlFlow, ptr::NonNull};

use crate::{
    acpi::{
        aml::{
            executor::{BreakKind, Executor},
            namespace::{NameSpace, init_namespace},
        },
        tables::{RsdpV1, TableManager},
    },
    lib::rust::spinlock::{SpinGuard, Spinlock},
    printk,
};

pub mod aml;
pub mod tables;

static ACPI: Spinlock<Option<Acpi>> = Spinlock::new(None);

pub trait AcpiArchInterface {
    fn get_rsdp() -> Option<NonNull<RsdpV1>>;

    fn io_in_u8(port: u16) -> u8;
    fn io_in_u16(port: u16) -> u16;
    fn io_in_u32(port: u16) -> u32;
    fn io_out_u8(port: u16, value: u8);
    fn io_out_u16(port: u16, value: u16);
    fn io_out_u32(port: u16, value: u32);
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
        let bytecode = aml::Bytecode::new(dsdt.aml_bytes());

        let guard = NameSpace::root().lock_pinned();
        let mut parser = aml::Parser::new(bytecode, guard.as_ref().get_ref());
        let _ = parser.parse();

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

#[unsafe(no_mangle)]
pub extern "C" fn acpi_print_namespace() {
    // NameSpace::root().lock_pinned().print_tree();
    let guard = NameSpace::root().lock_pinned();
    let root = guard.as_ref().get_ref();
    let current = root.get_by_path(&[b"_SB_", b"LNKA", b"_STA"]).unwrap();
    let _sta = current.try_into().ok().unwrap();

    let mut executor = Executor::new(_sta, &[], root, current);
    let return_value = executor.execute().unwrap();
    match return_value {
        ControlFlow::Break(BreakKind::Return(obj)) => {
            printk!("AML Method returns {:?}\n", obj)
        }
        _ => {
            printk!("AML Method returns None\n")
        }
    }
}

pub fn acpi<'a>() -> SpinGuard<'a, &'a mut Option<Acpi>> {
    ACPI.lock()
}
