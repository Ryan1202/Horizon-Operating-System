use crate::acpi::aml::{
    Parser,
    parser::{
        namespace::{Alias, Name, Scope},
        object::fields::{Device, Field, Method, Mutex, OpRegion, PowerRes, Processor},
        op::Opcode,
    },
};

pub mod field_element;
pub mod fields;

pub enum Object {
    DefAlias(Alias),
    DefName(Name),
    DefScope(Scope),
    NamedObject(NamedObject),
}

impl Object {
    pub fn parse<'a>(parser: &mut Parser<'a>, opcode: Opcode) -> Option<Self> {
        match opcode {
            Opcode::Alias => Some(Object::DefAlias(Alias::from_bytes(&mut parser.bytecode)?)),
            Opcode::Name => Some(Object::DefName(Name::parse(parser)?)),
            Opcode::Scope => Some(Object::DefScope(Scope::parse(parser)?)),
            _ => NamedObject::parse(parser, opcode).map(Object::NamedObject),
        }
    }
}

pub enum NamedObject {
    DefBankField,
    DefOpRegion(OpRegion),
    DefField(Field),
    DefMethod(Method),
    DefDevice(Device),
    DefMutex(Mutex),
    DefProccessor(Processor),
    DefPowerRes(PowerRes),
}

impl NamedObject {
    pub fn parse<'a>(parser: &mut Parser<'a>, opcode: Opcode) -> Option<Self> {
        match opcode {
            Opcode::OpRegion => Some(NamedObject::DefOpRegion(OpRegion::parse(parser)?)),
            Opcode::Field => Some(NamedObject::DefField(Field::parse(parser)?)),
            Opcode::Method => Some(NamedObject::DefMethod(Method::parse(parser)?)),
            Opcode::Device => Some(NamedObject::DefDevice(Device::parse(parser)?)),
            Opcode::Mutex => Some(NamedObject::DefMutex(Mutex::parse(parser)?)),
            Opcode::PowerRes => Some(NamedObject::DefPowerRes(PowerRes::parse(parser)?)),
            Opcode::Processor => Some(NamedObject::DefProccessor(Processor::parse(parser)?)),
            _ => None,
        }
    }
}

pub struct ObjectList;

impl ObjectList {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        while let Some(opcode) = Opcode::from(&mut parser.bytecode) {
            if let Some(_object) = Object::parse(parser, opcode) {
                continue;
            }
            break;
        }

        Some(ObjectList)
    }
}
