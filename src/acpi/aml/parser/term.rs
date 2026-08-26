use crate::acpi::aml::{
    Parser,
    evaluator::{BuiltinObject, Evaluatable},
    parser::{data::DataObject, object::Object, op::Opcode},
};

pub struct TermObj;

impl TermObj {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        let opcode = Opcode::from(&mut parser.bytecode)?;
        if let Some(_) = Object::parse(parser, opcode) {
            return Some(());
        }

        // TODO

        None
    }
}

pub(in crate::acpi) enum TermArg {
    ExpressionOpcode,
    DataObject(DataObject),
    Arg(u8),
    Local(u8),
}

impl TermArg {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let opcode = Opcode::from(&mut parser.bytecode)?;
        DataObject::parse(opcode, parser)
            .map(TermArg::DataObject)
            .or_else(|| match opcode {
                Opcode::Arg(u8) => Some(Self::Arg(u8)),
                Opcode::Local(u8) => Some(Self::Local(u8)),
                _ => None,
            })
    }
}

impl From<TermArg> for Evaluatable {
    fn from(term_arg: TermArg) -> Evaluatable {
        match term_arg {
            TermArg::ExpressionOpcode => todo!(),
            TermArg::DataObject(data) => Evaluatable::DataObject(data.into()),
            TermArg::Arg(index) => Evaluatable::Builtin(BuiltinObject::Arg(index)),
            TermArg::Local(index) => Evaluatable::Builtin(BuiltinObject::Local(index)),
        }
    }
}

pub struct TermList;

impl TermList {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        while TermObj::parse(parser).is_some() {}

        Some(())
    }
}
