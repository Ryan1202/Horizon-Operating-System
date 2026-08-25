use core::{ffi::CStr, fmt, ops::Index, ptr::NonNull};

use alloc::{boxed::Box, vec::Vec};

use crate::{
    acpi::aml::namespace::{NameSpace, NameSpaceBinding, Object},
    kernel::memory::kmalloc::Kmalloc,
};

#[derive(Debug)]
pub enum DataObject {
    Integer(u64),
    String(&'static CStr),
    Buffer(&'static [u8]),
    Package(Box<[PackageElement], Kmalloc>),
    FieldUnit(NonNull<NameSpace>),
}

pub enum PackageElement {
    DataObject(Object),
    ObjectReference,
    NameSpaceReference(NonNull<NameSpace>),
}

impl fmt::Debug for PackageElement {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PackageElement::DataObject(obj) => write!(f, "{:?}", obj),
            PackageElement::ObjectReference => write!(f, "ObjectReference"),
            PackageElement::NameSpaceReference(_) => write!(f, "NameSpaceReference"),
        }
    }
}

#[derive(Debug)]
pub struct Package {
    elements: Vec<PackageElement, Kmalloc>,
}

impl Package {
    pub fn new(elements: Vec<PackageElement, Kmalloc>) -> Self {
        Self { elements }
    }
}

impl Index<usize> for Package {
    type Output = PackageElement;

    fn index(&self, index: usize) -> &Self::Output {
        &self.elements[index]
    }
}

#[derive(Debug)]
pub struct VarPackage {
    pub num_elements: (), // TODO
    pub initializer: Vec<PackageElement, Kmalloc>,
}
