use core::ptr::NonNull;

use crate::{
    acpi::aml::{
        Parser,
        evaluator::Evaluatable,
        namespace::{
            self,
            objects::{self, FieldAccessType, FieldUnit, FieldUpdateRule},
        },
        parser::{
            data::PkgLength,
            namestring::{NamePath, Namestring},
        },
    },
    lib::rust::spinlock::Spinlock,
};

pub struct NamedField;

impl NamedField {
    pub fn parse(parser: &mut Parser<'_>, attribute: &mut FieldUnitAttribute) -> Option<Self> {
        let bytecode = &mut parser.bytecode;
        let name = bytecode.read(4);
        let pkg_length = PkgLength::from_bytes(bytecode)?;

        let field = attribute.get_object(pkg_length.length());
        let object = match attribute.bank.clone() {
            Some((bank_name, bank_value)) => {
                let bank = parser.current.get(parser.root, &bank_name)?;
                namespace::Object::BankField(objects::BankField {
                    bank: NonNull::from_ref(bank),
                    bank_value,
                    field,
                })
            }
            None => namespace::Object::FieldUnit(field),
        };
        let _ = parser.current.get_or_insert(
            parser.root,
            &Namestring::Relative {
                level: 0,
                path: NamePath(unsafe { name.as_chunks_unchecked() }),
            },
            object,
        )?;

        Some(Self)
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
                attribute.bit_offset = pkg_length.length() as u64 * 8;
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

pub struct FieldUnitAttribute<'a> {
    operation_region: NonNull<namespace::NameSpace>,
    bit_offset: u64,
    access_type: FieldAccessType,
    lock_rule: bool,
    update_rule: FieldUpdateRule,
    bank: Option<(Namestring<'a>, Evaluatable)>,
}

impl<'a> FieldUnitAttribute<'a> {
    fn get_object(&mut self, length: usize) -> FieldUnit {
        let lock = if self.lock_rule {
            Some(Spinlock::new(()))
        } else {
            None
        };

        self.bit_offset += length as u64;

        FieldUnit {
            region: self.operation_region,
            bit_offset: self.bit_offset as u32,
            bit_length: length as u32,
            access_type: self.access_type,
            lock,
            update_rule: self.update_rule,
        }
    }
}

impl<'a> FieldUnitAttribute<'a> {
    fn decode_flags(flag: u8) -> Option<(FieldAccessType, bool, FieldUpdateRule)> {
        let access_type = (flag & 0b0000_0111).try_into().ok()?;

        let lock_rule = (flag & 0b0000_1000) != 0;

        let update_rule = ((flag & 0b0011_0000) >> 4).try_into().ok()?;

        Some((access_type, lock_rule, update_rule))
    }

    pub fn new(
        operation_region: NonNull<namespace::NameSpace>,
        field_flags: u8,
        bank: Option<(Namestring<'a>, Evaluatable)>,
    ) -> Option<Self> {
        let (access_type, lock_rule, update_rule) = Self::decode_flags(field_flags)?;

        Some(FieldUnitAttribute {
            operation_region,
            bit_offset: 0,
            access_type,
            lock_rule,
            update_rule,
            bank,
        })
    }

    fn update(&mut self, flag: u8) -> Option<()> {
        let (access_type, lock_rule, update_rule) = Self::decode_flags(flag)?;

        self.access_type = access_type;
        self.lock_rule = lock_rule;
        self.update_rule = update_rule;

        Some(())
    }
}

pub struct FieldList;

impl FieldList {
    pub fn parse(parser: &mut Parser<'_>, mut attribute: FieldUnitAttribute) -> Option<Self> {
        while FieldElement::parse(parser, &mut attribute).is_some() {}

        Some(Self)
    }
}
