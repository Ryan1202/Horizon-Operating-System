use core::{array, ptr::NonNull, slice};

use alloc::vec::Vec;

use crate::{
    acpi::aml::{
        Bytecode, Parser,
        namespace::{self, data},
        parser::{
            namestring::Namestring,
            op::{self},
            prefix,
        },
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub(in crate::acpi) enum ComputationalData {
    Byte(u8),
    Word(u16),
    Dword(u32),
    Qword(u64),
    String(&'static [u8]),
    RevisionOp,
    Buffer(Buffer),
}

pub(in crate::acpi) enum DataObject {
    String(&'static [u8]),
    Integer(u64),
    RevisionOp,
    Buffer(Buffer),
    DefPackage(DefPackage),
    DefVarPackage(DefVarPackage),
}

impl DataObject {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let bytecode = &mut parser.bytecode;
        let byte = bytecode.next()?;

        match byte {
            op::ZERO_OP => Some(DataObject::Integer(0)),
            op::ONE_OP => Some(DataObject::Integer(1)),
            op::ONES_OP => Some(DataObject::Integer(!0)),
            prefix::BYTE_PREFIX => {
                let byte = bytecode.next()?;
                Some(DataObject::Integer(byte.into()))
            }
            prefix::WORD_PREFIX => {
                let bytes = array::from_fn(|_| bytecode.next().unwrap());
                let word = u16::from_le_bytes(bytes);
                Some(DataObject::Integer(word.into()))
            }
            prefix::DWORD_PREFIX => {
                let bytes = array::from_fn(|_| bytecode.next().unwrap());
                let dword = u32::from_le_bytes(bytes);
                Some(DataObject::Integer(dword.into()))
            }
            prefix::QWORD_PREFIX => {
                let bytes = array::from_fn(|_| bytecode.next().unwrap());
                let qword = u64::from_le_bytes(bytes);
                Some(DataObject::Integer(qword.into()))
            }
            prefix::STRING_PREFIX => {
                let mut length = 0;
                let mut bc = bytecode.clone();
                while let Some(c) = bytecode.next()
                    && c != b'\0'
                {
                    length += 1;
                }
                Some(DataObject::String(bc.read(length)))
            }
            prefix::EXT_OP_PREFIX if let Some(op::ext::REVISION_OP) = bytecode.next() => {
                Some(DataObject::RevisionOp)
            }
            op::BUFFER_OP => Some(DataObject::Buffer(Buffer::from_bytes(bytecode)?)),
            op::PACKAGE_OP => Some(DataObject::DefPackage(DefPackage::parse(parser)?)),
            op::VAR_PACKAGE_OP => Some(DataObject::DefVarPackage(DefVarPackage::from_bytes(
                bytecode,
            )?)),
            _ => None,
        }
    }
}

pub(in crate::acpi) enum DataRefObject {
    DataObject(DataObject),
    ObjectReference,
}

impl DataRefObject {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let data_object = DataObject::parse(parser)?;
        Some(DataRefObject::DataObject(data_object))
    }
}

fn parse_element(parser: &mut Parser<'_>) -> Option<data::PackageElement> {
    if let Some(data_ref) = DataRefObject::parse(parser) {
        match data_ref {
            DataRefObject::DataObject(data) => Some(match data {
                DataObject::Integer(int) => {
                    data::PackageElement::DataObject(namespace::Object::Integer(int))
                }
                DataObject::Buffer(buffer) => {
                    data::PackageElement::DataObject(namespace::Object::Buffer(
                        buffer.data.to_vec_in(Kmalloc::default()).into_boxed_slice(),
                    ))
                }
                DataObject::String(str) => {
                    data::PackageElement::DataObject(namespace::Object::String(
                        unsafe { slice::from_raw_parts(str.as_ptr().cast(), str.len()) }
                            .to_vec_in(Kmalloc::default())
                            .into_boxed_slice(),
                    ))
                }
                DataObject::RevisionOp => {
                    data::PackageElement::DataObject(namespace::Object::Revision)
                }
                DataObject::DefPackage(package) => data::PackageElement::DataObject(
                    namespace::Object::Package(data::Package::new(package.elements)),
                ),
                DataObject::DefVarPackage(_) => todo!(),
            }),
            DataRefObject::ObjectReference => Some(data::PackageElement::ObjectReference),
        }
    } else if let Some(namestring) = Namestring::from_bytes(&mut parser.bytecode) {
        let namespace = NonNull::from_ref(parser.current.get(parser.root, &namestring)?.0);
        Some(data::PackageElement::NameSpaceReference(namespace))
    } else {
        None
    }
}

pub(in crate::acpi) struct DefPackage {
    pub elements: Vec<data::PackageElement, Kmalloc>,
}

impl DefPackage {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let elements = Self::parse_package(parser)?;

        Some(Self { elements })
    }

    pub fn parse_package(parser: &mut Parser<'_>) -> Option<Vec<data::PackageElement, Kmalloc>> {
        let bytecode = &mut parser.bytecode;
        let length = PkgLength::from_bytes(bytecode)?;
        let num_elements = bytecode.next()?;

        let mut slice = parser.slice(length.len() - 1)?;

        let mut elements = Vec::with_capacity_in(num_elements as usize, Kmalloc::default());
        for _ in 0..num_elements {
            if let Some(element) = parse_element(&mut slice) {
                elements.push(element);
            } else {
                return None;
            }
        }

        Some(elements)
    }
}

pub(in crate::acpi) struct DefVarPackage {
    length: PkgLength,
    package: &'static [u8],
}

impl DefVarPackage {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let length = PkgLength::from_bytes(bytecode)?;
        let package = bytecode.read(length.len());

        for _ in 0..length.len() {
            let _ = bytecode.next();
        }

        Some(Self { length, package })
    }
}

pub(in crate::acpi) struct Buffer {
    pub data: &'static [u8],
}

impl Buffer {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let length = PkgLength::from_bytes(bytecode)?;
        let data = bytecode.read(length.len());

        Some(Buffer { data })
    }
}

pub(in crate::acpi) struct PkgLength(u32, usize);

impl PkgLength {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let first_byte = bytecode.next()?;
        let byte_count = (first_byte >> 6) as usize;
        let mut length = (first_byte & 0x3F) as u32;

        for i in 0..byte_count {
            let next_byte = bytecode.next()?;
            length |= (next_byte as u32) << (4 + (i * 8));
        }

        Some(PkgLength(length, byte_count))
    }

    pub const fn len(&self) -> usize {
        self.0 as usize - self.1 - 1
    }

    pub const fn raw_len(&self) -> usize {
        self.0 as usize
    }
}
