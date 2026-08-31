use crate::acpi::aml::{
    Parser,
    namespace::objects::CreateFieldType,
    opcode::Opcode,
    parser::{
        namespace::{Alias, Name, Scope},
        object::named::{
            BankField, CreateField, DataRegion, Device, Field, Method, Mutex, OpRegion, PowerRes,
            Processor, ThermalZone,
        },
    },
};

pub mod field_element;
pub mod named;

pub struct Object;

impl Object {
    pub fn parse<'a>(parser: &mut Parser<'a>, opcode: Opcode) -> Option<()> {
        match opcode {
            Opcode::Alias => Alias::parse(parser)?,
            Opcode::Name => Name::parse(parser)?,
            Opcode::Scope => Scope::parse(parser)?,
            _ => {
                return NamedObject::parse(parser, opcode);
            }
        }
        Some(())
    }
}

pub struct NamedObject;

impl NamedObject {
    pub fn parse<'a>(parser: &mut Parser<'a>, opcode: Opcode) -> Option<()> {
        match opcode {
            Opcode::BankField => BankField::parse(parser)?,
            Opcode::CreateBitField => CreateField::parse_fixed(parser, CreateFieldType::Bit)?,
            Opcode::CreateByteField => CreateField::parse_fixed(parser, CreateFieldType::Byte)?,
            Opcode::CreateWordField => CreateField::parse_fixed(parser, CreateFieldType::Word)?,
            Opcode::CreateDwordField => CreateField::parse_fixed(parser, CreateFieldType::Dword)?,
            Opcode::CreateQwordField => CreateField::parse_fixed(parser, CreateFieldType::Qword)?,
            Opcode::CreateField => CreateField::parse_arbitrary(parser)?,
            Opcode::DataRegion => DataRegion::parse(parser)?,
            Opcode::Event => named::Event::parse(parser)?,
            Opcode::External => named::External::parse(parser)?,
            Opcode::OpRegion => OpRegion::parse(parser)?,
            Opcode::Field => Field::parse(parser)?,
            Opcode::Method => Method::parse(parser)?,
            Opcode::Device => Device::parse(parser)?,
            Opcode::Mutex => Mutex::parse(parser)?,
            Opcode::PowerRes => PowerRes::parse(parser)?,
            Opcode::Processor => Processor::parse(parser)?,
            Opcode::ThermalZone => ThermalZone::parse(parser)?,
            _ => {
                return None;
            }
        }
        Some(())
    }
}

pub struct ObjectList;

impl ObjectList {
    pub fn parse<'a>(parser: &mut Parser<'a>) -> Option<Self> {
        while let Ok(opcode) = Opcode::parse(&mut parser.bytecode) {
            if let Some(_object) = Object::parse(parser, opcode) {
                continue;
            }
            break;
        }

        Some(ObjectList)
    }
}
