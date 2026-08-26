use core::{ptr::NonNull, slice};

use alloc::{boxed::Box, vec::Vec};

use crate::{
    acpi::aml::{
        Bytecode, Parser,
        evaluator::{self, Evaluatable, IndexOf},
        namespace::{self, data},
        parser::{namestring::Namestring, op::Opcode, term::TermArg},
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub(in crate::acpi) enum DataObject {
    String(&'static [u8]),
    Integer(u64),
    RevisionOp,
    Buffer(Buffer),
    DefPackage(DefPackage),
    DefVarPackage(DefVarPackage),
}

impl DataObject {
    pub fn parse(op: Opcode, parser: &mut Parser<'_>) -> Option<Self> {
        let bytecode = &mut parser.bytecode;

        match op {
            Opcode::Zero => Some(DataObject::Integer(0)),
            Opcode::One => Some(DataObject::Integer(1)),
            Opcode::Ones => Some(DataObject::Integer(!0)),
            Opcode::Byte => Some(DataObject::Integer(bytecode.next()?.into())),
            Opcode::Word => Some(DataObject::Integer(bytecode.read_u16()?.into())),
            Opcode::Dword => Some(DataObject::Integer(bytecode.read_u32()?.into())),
            Opcode::Qword => Some(DataObject::Integer(bytecode.read_u64()?.into())),
            Opcode::String => {
                let mut length = 0;
                let mut bc = bytecode.clone();
                while let Some(c) = bytecode.next()
                    && c != b'\0'
                {
                    length += 1;
                }
                Some(DataObject::String(bc.read(length)))
            }
            Opcode::Revision => Some(DataObject::RevisionOp),
            Opcode::Buffer => Some(DataObject::Buffer(Buffer::from_bytes(bytecode)?)),
            Opcode::Package => Some(DataObject::DefPackage(DefPackage::parse(parser)?)),
            Opcode::VarPackage => Some(DataObject::DefVarPackage(DefVarPackage::parse(parser)?)),
            _ => None,
        }
    }
}

impl From<DataObject> for namespace::Object {
    fn from(data_object: DataObject) -> namespace::Object {
        use namespace::Object;
        match data_object {
            DataObject::Integer(int) => Object::Integer(int),
            DataObject::Buffer(buffer) => {
                Object::Buffer(buffer.data.to_vec_in(Kmalloc::default()).into_boxed_slice())
            }
            DataObject::String(str) => Object::String(
                unsafe { slice::from_raw_parts(str.as_ptr().cast(), str.len()) }
                    .to_vec_in(Kmalloc::default())
                    .into_boxed_slice(),
            ),
            DataObject::RevisionOp => Object::Revision,
            DataObject::DefPackage(package) => {
                Object::Package(data::Package::new(package.elements))
            }
            DataObject::DefVarPackage(package) => Object::VarPackage(data::VarPackage::new(
                package.num_elements,
                package.elements,
            )),
        }
    }
}

pub(in crate::acpi) enum DataRefObject {
    DataObject(DataObject),
    ObjectReference(Evaluatable),
}

impl DataRefObject {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let opcode = Opcode::from(&mut parser.bytecode)?;
        match DataObject::parse(opcode, parser) {
            Some(data_object) => Some(DataRefObject::DataObject(data_object)),
            None => Reference::parse(opcode, parser).map(DataRefObject::ObjectReference),
        }
    }
}

fn parse_element(parser: &mut Parser<'_>) -> Option<data::PackageElement> {
    use data::PackageElement;
    if let Some(data_ref) = DataRefObject::parse(parser) {
        match data_ref {
            DataRefObject::DataObject(data) => Some(PackageElement::DataObject(data.into())),
            DataRefObject::ObjectReference(eval) => Some(PackageElement::ObjectReference(eval)),
        }
    } else if let Some(namestring) = Namestring::from_bytes(&mut parser.bytecode) {
        let namespace = NonNull::from_ref(parser.current.get(parser.root, &namestring)?.0);
        Some(PackageElement::NameSpaceReference(namespace))
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

        let mut slice = parser.slice(length.payload_length() - 1)?;

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
    pub num_elements: Box<Evaluatable, Kmalloc>,
    pub elements: Vec<data::PackageElement, Kmalloc>,
}

impl DefVarPackage {
    pub fn parse(parser: &mut Parser<'_>) -> Option<Self> {
        let pkg_length = PkgLength::from_bytes(&mut parser.bytecode)?;

        let mut slice = parser.slice(pkg_length.payload_length())?;
        let num_elements = Box::new_in(TermArg::parse(&mut slice)?.into(), Kmalloc::default());
        let elements = DefPackage::parse_package(&mut slice)?;

        Some(Self {
            num_elements,
            elements,
        })
    }
}

pub(in crate::acpi) struct Buffer {
    pub data: &'static [u8],
}

impl Buffer {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let length = PkgLength::from_bytes(bytecode)?;
        let data = bytecode.read(length.payload_length());

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

    pub const fn payload_length(&self) -> usize {
        self.0 as usize - self.1 - 1
    }

    pub const fn length(&self) -> usize {
        self.0 as usize
    }
}

pub struct SimpleName;

impl SimpleName {
    pub fn parse(
        parser: &mut Parser<'_>,
        super_name: bool,
    ) -> Option<Result<evaluator::SuperName, Opcode>> {
        let first = parser.bytecode.first()?;

        match first {
            b'A'..=b'Z' | b'^' | b'\\' | b'_' => {
                match Namestring::from_bytes(&mut parser.bytecode)? {
                    Namestring::Root(name) => Some(Ok(evaluator::SuperName::Name(
                        evaluator::Path::Root(name.to_boxed()),
                    ))),
                    Namestring::Relative { level, path } => {
                        Some(Ok(evaluator::SuperName::Name(evaluator::Path::Relative {
                            level,
                            path: path.to_boxed(),
                        })))
                    }
                }
            }
            _ => {
                let opcode = Opcode::from(&mut parser.bytecode.slice(2))?;
                match opcode {
                    Opcode::Arg(u8) => Some(Ok(evaluator::SuperName::Builtin(
                        evaluator::BuiltinObject::Arg(u8),
                    ))),
                    Opcode::Local(u8) => Some(Ok(evaluator::SuperName::Builtin(
                        evaluator::BuiltinObject::Local(u8),
                    ))),
                    Opcode::Debug if super_name => Some(Ok(evaluator::SuperName::Builtin(
                        evaluator::BuiltinObject::Debug,
                    ))),
                    _ => Some(Err(opcode)),
                }
            }
        }
    }
}

pub struct Reference;

impl Reference {
    pub fn parse(opcode: Opcode, parser: &mut Parser<'_>) -> Option<evaluator::Evaluatable> {
        let reference_type = Self::parse_one(opcode, parser)?;

        Some(evaluator::Evaluatable::Reference(reference_type))
    }

    fn parse_one(opcode: Opcode, parser: &mut Parser<'_>) -> Option<evaluator::ReferenceType> {
        match opcode {
            Opcode::RefOf => Some(evaluator::ReferenceType::RefOf(Self::parse_source(parser)?)),
            Opcode::CondRefOf => {
                let source = Self::parse_source(parser)?;

                let target = Self::parse_target(parser)?;

                Some(evaluator::ReferenceType::CondRefOf { source, target })
            }
            Opcode::DerefOf => {
                let nested = Box::new_in(
                    Self::parse_one(Opcode::from(&mut parser.bytecode)?, parser)?,
                    Kmalloc::default(),
                );
                Some(evaluator::ReferenceType::DerefOf(nested))
            }
            Opcode::Index => {
                let source = TermArg::parse(parser)?.into();
                let index = TermArg::parse(parser)?.into();
                let target = Self::parse_target(parser)?;

                let index_of = IndexOf {
                    source,
                    index,
                    target,
                };
                Some(evaluator::ReferenceType::IndexOf(Box::new_in(
                    index_of,
                    Kmalloc::default(),
                )))
            }
            _ => None,
        }
    }

    fn parse_source(parser: &mut Parser<'_>) -> Option<evaluator::SuperName> {
        let super_name = SimpleName::parse(parser, true)?;
        let super_name = super_name.map_or_else(
            |opcode| {
                let nested = Box::new_in(Self::parse_one(opcode, parser)?, Kmalloc::default());
                Some(evaluator::SuperName::Nested(nested))
            },
            |ref_type| Some(ref_type),
        )?;
        Some(super_name)
    }

    fn parse_target(parser: &mut Parser<'_>) -> Option<Option<evaluator::SuperName>> {
        let target = SimpleName::parse(parser, true)?;
        let target = target.map_or_else(
            |opcode| {
                if let Opcode::Zero = opcode {
                    // 这里使用值相等的 Opcode::Zero 代替 NullName，因为 NullName 不属于 Opcode
                    Some(None)
                } else {
                    let nested = Box::new_in(Self::parse_one(opcode, parser)?, Kmalloc::default());
                    Some(Some(evaluator::SuperName::Nested(nested)))
                }
            },
            |ref_type| Some(Some(ref_type)),
        )?;
        Some(target)
    }
}
