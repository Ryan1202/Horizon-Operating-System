use core::ptr::NonNull;

use crate::acpi::aml::{
    Parser,
    namespace::{self, Object},
    parser::{
        data::{DataRefObject, PkgLength},
        namestring::Namestring,
        term::TermList,
    },
};

pub struct Alias;

impl Alias {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let target = Namestring::from_bytes(&mut parser.bytecode)?;

        let target = parser.current.get(parser.root, &target)?;
        let target = NonNull::from_ref(target);

        let _ =
            parser
                .current
                .get_or_insert(parser.root, &name, namespace::Object::Alias(target))?;

        Some(())
    }
}

pub struct Name;

impl Name {
    pub fn parse(parser: &mut Parser<'_>) -> Option<()> {
        let name = Namestring::from_bytes(&mut parser.bytecode)?;
        let data_ref = DataRefObject::parse(parser)?;

        let object = match data_ref {
            DataRefObject::DataObject(data) => Object::Data(data),
            DataRefObject::ObjectReference(eval) => namespace::Object::ObjectReference(eval),
        };

        let _object = parser.current.get_or_insert(parser.root, &name, object)?;

        Some(())
    }
}

pub struct Scope;

impl Scope {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        let length = PkgLength::from_bytes(&mut parser.bytecode)?;
        let name = Namestring::from_bytes(&mut parser.bytecode)?;

        let current = parser
            .current
            .get_or_insert(parser.root, &name, namespace::Object::Scope)?;

        let slice_length = length.payload_length() as usize - name.bytecode_length();
        let mut slice = parser.enter_namespace(current, slice_length)?;

        TermList::parse(&mut slice)?;

        Some(())
    }
}
