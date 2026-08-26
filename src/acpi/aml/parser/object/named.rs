use core::ptr::NonNull;

use alloc::boxed::Box;

use crate::{
    acpi::aml::{
        Parser,
        evaluator::Evaluatable,
        executor::Executable,
        namespace::{
            self,
            objects::{self, CreateFieldType, OperationRegion, RegionSpace},
        },
        parser::{
            data::PkgLength,
            namestring::Namestring,
            object::{
                ObjectList,
                field_element::{FieldList, FieldUnitAttribute},
            },
            term::{TermArg, TermList},
        },
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub struct BankField;

impl BankField {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let mut slice = parser.slice(pkg_length.payload_length() as usize)?;

        let region_name = Namestring::from_bytes(&mut slice.bytecode)?;
        let bank_name = Namestring::from_bytes(&mut slice.bytecode)?;
        let bank_value = TermArg::parse(&mut slice)?.into();
        let field_flags = slice.bytecode.next()?;
        let bank = Some((bank_name, bank_value));

        let operation_region = NonNull::from_ref(slice.current.get(slice.root, &region_name)?.0);
        let attribute = FieldUnitAttribute::new(operation_region, field_flags, bank)?;
        let _ = FieldList::parse(&mut slice, attribute)?;

        Some(())
    }
}

pub struct CreateField;

impl CreateField {
    pub fn parse_fixed(
        parser: &mut Parser<'_>,
        f: impl FnOnce(Evaluatable) -> CreateFieldType,
    ) -> Option<()> {
        let source_buffer = TermArg::parse(parser)?.into();
        let index = TermArg::parse(parser)?.into();
        let namestring = Namestring::from_bytes(&mut parser.bytecode)?;

        let object = objects::CreateField {
            field_type: f(index),
            source: source_buffer,
        };
        let object = namespace::Object::CreateField(Box::new_in(object, Kmalloc::default()));
        let _ = parser
            .current
            .get_or_insert(parser.root, &namestring, object)?;

        Some(())
    }

    pub fn parse_arbitrary(parser: &mut Parser<'_>) -> Option<()> {
        let source_buffer = TermArg::parse(parser)?.into();
        let index = TermArg::parse(parser)?.into();
        let num_bits = TermArg::parse(parser)?.into();
        let namestring = Namestring::from_bytes(&mut parser.bytecode)?;

        let object = objects::CreateField {
            field_type: CreateFieldType::ArbitraryLength { index, num_bits },
            source: source_buffer,
        };
        let object = namespace::Object::CreateField(Box::new_in(object, Kmalloc::default()));
        let _ = parser
            .current
            .get_or_insert(parser.root, &namestring, object)?;

        Some(())
    }
}

pub struct DataRegion;

impl DataRegion {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let region_name = Namestring::from_bytes(&mut parser.bytecode)?;
        let signature = TermArg::parse(parser)?.into();
        let oem_id = TermArg::parse(parser)?.into();
        let oem_table_id = TermArg::parse(parser)?.into();

        let object = namespace::Object::DataTableRegion(Box::new_in(
            objects::DataTableRegion {
                signature,
                oem_id,
                oem_table_id,
            },
            Kmalloc::default(),
        ));
        let _ = parser
            .current
            .get_or_insert(parser.root, &region_name, object)?;

        Some(())
    }
}

pub struct OpRegion;

impl OpRegion {
    pub fn parse(parser: &mut Parser) -> Option<()> {
        let bytecode = &mut parser.bytecode;

        let region_name = Namestring::from_bytes(bytecode)?;
        let region_space = RegionSpace::from_byte(bytecode.next()?);
        let offset = Box::new_in(TermArg::parse(parser)?.into(), Kmalloc::default());
        let len = Box::new_in(TermArg::parse(parser)?.into(), Kmalloc::default());

        let object_region = namespace::Object::OperationRegion(Box::new_in(
            OperationRegion {
                region_space,
                offset,
                len,
            },
            Kmalloc::default(),
        ));
        let _ = parser
            .current
            .get_or_insert(parser.root, &region_name, object_region)?;

        Some(())
    }
}

pub struct Field;

impl Field {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let operation_region = parser.current.get(parser.root, &name)?.0;

        let slice_length = pkg_length.payload_length() as usize - name.bytecode_length();
        let mut slice = parser.slice(slice_length)?;

        let field_flags = slice.bytecode.next()?;
        let attribute =
            FieldUnitAttribute::new(NonNull::from_ref(operation_region), field_flags, None)?;

        let _ = FieldList::parse(&mut slice, attribute)?;

        Some(())
    }
}

pub struct Method;

impl Method {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let method_flags = parser.bytecode.next()?;

        let size = pkg_length.payload_length() as usize - name.bytecode_length() - 1;
        let bytecode = parser.bytecode.read(size);

        let _ = parser.current.get_or_insert(
            parser.root,
            &name,
            namespace::Object::Method(objects::Method {
                sync_level: method_flags >> 4,
                serialize: method_flags & 0x08 != 0,
                arg_count: method_flags & 0x07,
                bytecode: Executable::new(bytecode),
            }),
        )?;

        Some(())
    }
}

pub struct Device;

impl Device {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let current =
            parser
                .current
                .get_or_insert(parser.root, &name, namespace::Object::Device)?;

        let slice_length = pkg_length.payload_length() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;
        TermList::parse(&mut slice)?;

        Some(())
    }
}

pub struct Mutex;

impl Mutex {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let sync_flags = parser.bytecode.next()?;
        let sync_level = sync_flags & 0x0f;

        let object = namespace::Object::Mutex(objects::Mutex::new(sync_level));
        let _ = parser.current.get_or_insert(parser.root, &name, object)?;

        Some(())
    }
}

pub struct PowerRes;

impl PowerRes {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let system_level = parser.bytecode.next()?;
        let resource_order = parser.bytecode.read_u16()?;

        let object = namespace::Object::PowerResource(objects::PowerResource {
            system_level: system_level,
            resource_order: resource_order,
        });
        let current = parser.current.get_or_insert(parser.root, &name, object)?;

        let slice_length = pkg_length.payload_length() as usize - name.bytecode_length() - 3;
        let mut slice = parser.enter_namespace(current, slice_length)?;
        TermList::parse(&mut slice)?;

        Some(())
    }
}

pub struct Processor;

impl Processor {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let bytecode = &mut parser.bytecode;
        let pkg_length = PkgLength::from_bytes(bytecode)?;
        let name = Namestring::from_bytes(bytecode)?;

        let current =
            parser
                .current
                .get_or_insert(parser.root, &name, namespace::Object::Processor)?;

        let slice_length = pkg_length.payload_length() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;

        let _proc_id = slice.bytecode.next()?;
        let _pblk_addr = slice.bytecode.read(4);
        let _pblk_len = slice.bytecode.next()?;

        ObjectList::parse(&mut slice)?;

        Some(())
    }
}

pub struct External;

impl External {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let object_type = parser.bytecode.next()?.into();
        let arg_count = parser.bytecode.next()?;

        let object = namespace::Object::External(objects::External {
            object_type,
            arg_count,
        });
        let _ = parser.current.get_or_insert(parser.root, &name, object)?;

        Some(())
    }
}

pub struct ThermalZone;

impl ThermalZone {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let current =
            parser
                .current
                .get_or_insert(parser.root, &name, namespace::Object::ThermalZone)?;

        let slice_length = pkg_length.payload_length() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;
        TermList::parse(&mut slice)?;

        Some(())
    }
}

pub struct Event;

impl Event {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let object = namespace::Object::Event;
        let _ = parser.current.get_or_insert(parser.root, &name, object)?;

        Some(())
    }
}
