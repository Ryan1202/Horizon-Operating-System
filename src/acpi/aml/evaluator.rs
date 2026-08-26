use alloc::boxed::Box;

use crate::{
    acpi::aml::namespace::{Name, Object},
    kernel::memory::kmalloc::Kmalloc,
};

#[derive(Debug, Clone)]
pub enum Evaluatable {
    Reference(ReferenceType),
    Builtin(BuiltinObject),
    DataObject(Object),
}

#[derive(Debug, Clone)]
pub enum BuiltinObject {
    Arg(u8),
    Local(u8),
    Debug,
}

#[derive(Debug, Clone)]
pub enum Path {
    Root(Box<[Name], Kmalloc>),
    Relative {
        level: u8,
        path: Box<[Name], Kmalloc>,
    },
}
#[derive(Debug, Clone)]
pub enum SuperName {
    Name(Path),
    Builtin(BuiltinObject),
    Nested(Box<ReferenceType, Kmalloc>),
}

#[derive(Debug, Clone)]
pub struct IndexOf {
    pub source: Evaluatable,
    pub index: Evaluatable,
    pub target: Option<SuperName>,
}
#[derive(Debug, Clone)]
pub enum ReferenceType {
    RefOf(SuperName),
    CondRefOf {
        source: SuperName,
        target: Option<SuperName>,
    },
    DerefOf(Box<ReferenceType, Kmalloc>),
    IndexOf(Box<IndexOf, Kmalloc>),
}
