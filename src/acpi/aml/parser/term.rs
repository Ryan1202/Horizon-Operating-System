use alloc::vec::Vec;

use crate::{
    acpi::aml::{
        Bytecode, Parser,
        namespace::NameSpace,
        parser::{data::DataObject, object::Object, op::Opcode},
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub enum TermObj {
    Object(Object),
    StatementOpcode,
    ExpressionOpcode,
}

impl TermObj {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        let opcode = Opcode::from(&mut parser.bytecode)?;
        if let Some(object) = Object::parse(parser, opcode) {
            return Some(TermObj::Object(object));
        }

        // ...

        None
    }
}

pub enum TermArg {
    ExpressionOpcode,
    DataObject(DataObject),
    ArgObj,
    LocalObj,
}

impl TermArg {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        DataObject::parse(parser).map(TermArg::DataObject)
    }
}

pub struct TermList;

impl TermList {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        while let Some(_) = TermObj::parse(parser) {}

        Some(())
    }
}
