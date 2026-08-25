use crate::acpi::aml::{
    Parser,
    executor::Executable,
    namespace::{
        self,
        objects::{self, OperationRegion, RegionSpace},
    },
    parser::{
        data::PkgLength,
        namestring::Namestring,
        object::{
            ObjectList,
            field_element::{FieldFlags, FieldList, FieldUnitAttribute},
        },
        term::{TermArg, TermList},
    },
};

pub struct BankField {
    region_name: Namestring,
    bank_name: Namestring,
    term_arg: (),
    field_flags: FieldFlags,
    field_list: FieldList,
}

// impl BankField {
//     pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
//         let pkg_length = PkgLength::from_bytes(bytecode)?;
//         let region_name = Namestring::from_bytes(bytecode)?;
//         let bank_name = Namestring::from_bytes(bytecode)?;
//         let term_arg = todo!();
//         let field_flags = FieldFlags(bytecode.next()?);
//         let field_list = FieldList::parse(bytecode)?;

//         Some(BankField {
//             region_name,
//             bank_name,
//             term_arg,
//             field_flags,
//             field_list,
//         })
//     }
// }

pub struct OpRegion {
    offset: TermArg,
    length: TermArg,
}

impl OpRegion {
    pub fn parse(parser: &mut Parser) -> Option<Self> {
        let bytecode = &mut parser.bytecode;

        let region_name = Namestring::from_bytes(bytecode)?;
        let region_space = RegionSpace::from_byte(bytecode.next()?);
        let offset = TermArg::parse(parser)?;
        let length = TermArg::parse(parser)?;

        let object_region = namespace::Object::Region(OperationRegion {
            region_space,
            offset: (),
            len: (),
        });
        let _ = parser
            .current
            .get_or_insert(parser.root, &region_name, object_region)?;

        Some(OpRegion { offset, length })
    }
}

pub struct Field;

impl Field {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let current = parser
            .current
            .insert(name.last_name()?, namespace::Object::Field);

        let slice_length = pkg_length.len() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;

        let field_flags = FieldFlags(slice.bytecode.next()?);
        let attribute = FieldUnitAttribute::new(field_flags)?;

        let _ = FieldList::parse(&mut slice, attribute)?;

        Some(Self)
    }
}

pub struct Method;

impl Method {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let method_flags = parser.bytecode.next()?;

        let size = pkg_length.len() as usize - name.bytecode_length() - 1;
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

        Some(Self)
    }
}

pub struct Device;

impl Device {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let current =
            parser
                .current
                .get_or_insert(parser.root, &name, namespace::Object::Device)?;

        let slice_length = pkg_length.len() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;
        TermList::parse(&mut slice)?;

        Some(Self)
    }
}

pub struct Mutex;

impl Mutex {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let sync_flags = parser.bytecode.next()?;
        let sync_level = sync_flags & 0x0f;

        let object = namespace::Object::Mutex(objects::Mutex::new(sync_level));
        let _ = parser.current.get_or_insert(parser.root, &name, object)?;

        Some(Self)
    }
}

pub struct SystemLevel(u8);
pub struct ResourceOrder(u16);

pub struct PowerRes;

impl PowerRes {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let system_level = parser.bytecode.next()?;
        let resource_order = u16::from_le_bytes([parser.bytecode.next()?, parser.bytecode.next()?]);

        let object = namespace::Object::PowerResource(objects::PowerResource {
            system_level: system_level,
            resource_order: resource_order,
        });
        let current = parser.current.get_or_insert(parser.root, &name, object)?;

        let slice_length = pkg_length.len() as usize - name.bytecode_length() - 3;
        let mut slice = parser.enter_namespace(current, slice_length)?;
        TermList::parse(&mut slice)?;

        Some(Self)
    }
}

pub struct Processor;

impl Processor {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let bytecode = &mut parser.bytecode;
        let pkg_length = PkgLength::from_bytes(bytecode)?;
        let name = Namestring::from_bytes(bytecode)?;

        let current =
            parser
                .current
                .get_or_insert(parser.root, &name, namespace::Object::Processor)?;

        let slice_length = pkg_length.len() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;

        let _proc_id = slice.bytecode.next()?;
        let _pblk_addr = slice.bytecode.read(4);
        let _pblk_len = slice.bytecode.next()?;

        ObjectList::parse(&mut slice)?;

        Some(Processor)
    }
}
