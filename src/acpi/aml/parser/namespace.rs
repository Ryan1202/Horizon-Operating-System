use core::slice;

use crate::{
    acpi::aml::{
        Bytecode, Parser,
        namespace::{self, data},
        parser::{
            data::{DataObject, DataRefObject, PkgLength},
            namestring::Namestring,
            term::TermList,
        },
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub struct Alias(Namestring, Namestring);

impl Alias {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let name = Namestring::from_bytes(bytecode)?;
        let target = Namestring::from_bytes(bytecode)?;

        Some(Alias(name, target))
    }
}

pub struct Name;

impl Name {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let data_ref = DataRefObject::parse(parser)?;

        let object = match data_ref {
            DataRefObject::DataObject(data) => match data {
                DataObject::Integer(int) => namespace::Object::Integer(int.into()),
                DataObject::String(str) => namespace::Object::String(unsafe {
                    slice::from_raw_parts(str.as_ptr().cast(), str.len())
                        .to_vec_in(Kmalloc::default())
                        .into_boxed_slice()
                }),
                DataObject::Buffer(buffer) => namespace::Object::Buffer(unsafe {
                    slice::from_raw_parts(buffer.data.as_ptr().cast(), buffer.data.len())
                        .to_vec_in(Kmalloc::default())
                        .into_boxed_slice()
                }),
                DataObject::RevisionOp => namespace::Object::Revision,
                DataObject::DefPackage(package) => {
                    namespace::Object::Package(data::Package::new(package.elements))
                }
                DataObject::DefVarPackage(_) => todo!(),
            },
            _ => todo!(),
        };

        let _object = parser.current.get_or_insert(parser.root, &name, object)?;

        Some(Self)
    }
}

pub struct Scope;

impl Scope {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        let length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let current = parser
            .current
            .get_or_insert(parser.root, &name, namespace::Object::Scope)?;

        let slice_length = length.len() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;

        TermList::parse(&mut slice)?;

        Some(Self)
    }
}
