use crate::{
    acpi::aml::{
        Bytecode, Parser,
        namespace::{
            self,
            objects::{FieldAccessType, FieldUnit, FieldUpdateRule},
        },
        parser::{
            data::PkgLength,
            namestring::{NamePath, Namestring},
        },
    },
    lib::rust::spinlock::SpinlockRaw,
};

pub struct FieldFlags(pub u8);

pub struct NamedField;

impl NamedField {
    pub fn parse(parser: &mut Parser<'_>, attribute: &mut FieldUnitAttribute) -> Option<Self> {
        let bytecode = &mut parser.bytecode;
        let name = bytecode.read(4);
        let pkg_length = PkgLength::from_bytes(bytecode)?;

        let object = namespace::Object::FieldUnit(attribute.get_object(pkg_length.raw_len()));
        let _ = parser.current.get_or_insert(
            parser.root,
            &Namestring::Relative {
                level: 0,
                path: NamePath::Single(name),
            },
            object,
        )?;

        Some(Self)
    }
}

pub struct ExtendedAccessAttribute(u8);

pub struct ExtendedAccessField {
    access_type: u8,
    attribute: ExtendedAccessAttribute,
    length: u8,
}

impl ExtendedAccessField {
    pub fn parse(bytecode: &mut Bytecode) -> Option<Self> {
        let access_type = bytecode.next()?;
        let attribute = ExtendedAccessAttribute(bytecode.next()?);
        let length = bytecode.next()?;

        Some(ExtendedAccessField {
            access_type,
            attribute,
            length,
        })
    }
}

pub struct FieldElement;

impl FieldElement {
    pub fn parse(parser: &mut Parser<'_>, attribute: &mut FieldUnitAttribute) -> Option<Self> {
        let bytecode = &mut parser.bytecode;
        let first_byte = bytecode.first()?;

        match first_byte {
            0x00 => {
                let _ = bytecode.next();
                let pkg_length = PkgLength::from_bytes(bytecode)?;
                attribute.offset += pkg_length.raw_len();
                Some(Self)
            }
            0x01 => {
                let _ = bytecode.next();

                attribute.update(bytecode.next()?)?;
                Some(Self)
            }
            0x02 => {
                let _ = bytecode.next();
                todo!("ConnectField is not implemented yet");
            }
            0x03 => {
                let _ = bytecode.next();
                todo!("ExtendedAccessField is not implemented yet");
            }
            _ => {
                NamedField::parse(parser, attribute)?;
                Some(Self)
            }
        }
    }
}

#[derive(Clone)]
enum AccessType {
    Any,
    Byte,
    Word,
    Dword,
    Qword,
    Block,
}

#[derive(Clone)]
enum UpdateRule {
    Preserve,
    WriteAsOnes,
    WriteAsZeros,
}

#[derive(Clone)]
pub struct FieldUnitAttribute {
    offset: usize,
    access_type: AccessType,
    lock_rule: bool,
    update_rule: UpdateRule,
}

impl FieldUnitAttribute {
    fn get_object(&mut self, length: usize) -> FieldUnit {
        let access_type = match self.access_type {
            AccessType::Any => FieldAccessType::Any,
            AccessType::Byte => FieldAccessType::Byte,
            AccessType::Word => FieldAccessType::Word,
            AccessType::Dword => FieldAccessType::Dword,
            AccessType::Qword => FieldAccessType::Qword,
            AccessType::Block => FieldAccessType::Block,
        };

        let update_rule = match self.update_rule {
            UpdateRule::Preserve => FieldUpdateRule::Preserve,
            UpdateRule::WriteAsOnes => FieldUpdateRule::WriteAsOnes,
            UpdateRule::WriteAsZeros => FieldUpdateRule::WriteAsZeros,
        };

        let lock = if self.lock_rule {
            Some(SpinlockRaw::new_unlocked())
        } else {
            None
        };

        self.offset += length;

        FieldUnit {
            access_type,
            lock,
            update_rule,
            length: length as u32,
        }
    }
}

impl FieldUnitAttribute {
    pub fn new(field_flags: FieldFlags) -> Option<Self> {
        let access_type = match field_flags.0 & 0b0000_0111 {
            0 => AccessType::Any,
            1 => AccessType::Byte,
            2 => AccessType::Word,
            3 => AccessType::Dword,
            4 => AccessType::Qword,
            5 => AccessType::Block,
            _ => return None,
        };

        let lock_rule = (field_flags.0 & 0b0000_1000) != 0;

        let update_rule = match (field_flags.0 & 0b0011_0000) >> 4 {
            0 => UpdateRule::Preserve,
            1 => UpdateRule::WriteAsOnes,
            2 => UpdateRule::WriteAsZeros,
            _ => return None,
        };

        Some(FieldUnitAttribute {
            offset: 0,
            access_type,
            lock_rule,
            update_rule,
        })
    }

    fn update(&mut self, flag: u8) -> Option<()> {
        let access_type = match flag & 0b0000_0111 {
            0 => AccessType::Any,
            1 => AccessType::Byte,
            2 => AccessType::Word,
            3 => AccessType::Dword,
            4 => AccessType::Qword,
            5 => AccessType::Block,
            _ => return None,
        };

        let lock_rule = (flag & 0b0000_1000) != 0;

        let update_rule = match (flag & 0b0011_0000) >> 4 {
            0 => UpdateRule::Preserve,
            1 => UpdateRule::WriteAsOnes,
            2 => UpdateRule::WriteAsZeros,
            _ => return None,
        };

        self.access_type = access_type;
        self.lock_rule = lock_rule;
        self.update_rule = update_rule;

        Some(())
    }
}

pub struct FieldList;

impl FieldList {
    pub fn parse(parser: &mut Parser<'_>, mut attribute: FieldUnitAttribute) -> Option<Self> {
        while let Some(_) = FieldElement::parse(parser, &mut attribute) {}

        Some(Self)
    }
}
