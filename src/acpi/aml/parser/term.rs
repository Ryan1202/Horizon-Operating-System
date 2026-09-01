use core::ptr::NonNull;

use crate::acpi::aml::{
    Parser,
    evaluator::{BuiltinObject, Evaluatable},
    namespace::{
        self, NameSpace,
        objects::{DataObject, Method},
    },
    opcode::Opcode,
    parser::{namestring::Namestring, object::Object},
};

pub struct TermObj;

impl TermObj {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<()> {
        let opcode = Opcode::parse(&mut parser.bytecode).ok()?;
        if let Some(_) = Object::parse(parser, opcode) {
            return Some(());
        }

        // TODO

        None
    }
}

pub(in crate::acpi) enum TermArg {
    ExpressionOpcode,
    MethodInvocation((NonNull<NameSpace>, NonNull<Method>)),
    Object(NonNull<namespace::Object>),
    DataObject(DataObject),
    Arg(u8),
    Local(u8),
}

impl TermArg {
    pub fn parse(parser: &mut Parser<'_>) -> Result<Self, Option<Opcode>> {
        let mut _bc = parser.bytecode.clone();
        match Opcode::parse(&mut _bc) {
            Ok(opcode) => {
                parser.bytecode = _bc;
                DataObject::parse(opcode, parser)
                    .map(TermArg::DataObject)
                    .map_or_else(
                        || match opcode {
                            Opcode::Arg(u8) => Ok(Self::Arg(u8)),
                            Opcode::Local(u8) => Ok(Self::Local(u8)),
                            _ => Err(Some(opcode)),
                        },
                        Ok,
                    )
            }
            Err(Some(byte)) if b'A' <= byte && byte <= b'Z' || byte == b'_' => {
                let name = Namestring::from_bytes(&mut parser.bytecode).ok_or(None)?;
                let object = parser.current.get(parser.root, &name).ok_or(None)?;
                let arg = match object.object() {
                    namespace::Object::Method(method) => Self::MethodInvocation((
                        NonNull::from_ref(object),
                        NonNull::from_ref(method),
                    )),
                    _ => Self::Object(NonNull::from_ref(object.object())),
                };
                Ok(arg)
            }
            Err(_) => Err(None),
        }
    }
}

impl From<TermArg> for Evaluatable {
    fn from(term_arg: TermArg) -> Evaluatable {
        match term_arg {
            TermArg::ExpressionOpcode => todo!(),
            TermArg::Object(_) => {
                panic!("Object should not be converted to Evaluatable directly")
            }
            TermArg::MethodInvocation(_) => unreachable!(),
            TermArg::DataObject(data) => Evaluatable::DataObject(data),
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
