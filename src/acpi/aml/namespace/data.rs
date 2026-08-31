use core::{fmt, ops::Index, ptr::NonNull};

use alloc::{boxed::Box, vec::Vec};

use crate::{
    acpi::aml::{
        evaluator::Evaluatable,
        namespace::{NameSpace, objects::DataObject},
    },
    kernel::memory::kmalloc::Kmalloc,
};

#[derive(Clone)]
pub enum PackageElement {
    DataObject(DataObject),
    ObjectReference(Evaluatable),
    NameSpaceReference(NonNull<NameSpace>),
}

impl fmt::Debug for PackageElement {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PackageElement::DataObject(obj) => write!(f, "{:?}", obj),
            PackageElement::ObjectReference(_) => write!(f, "ObjectReference"),
            PackageElement::NameSpaceReference(_) => write!(f, "NameSpaceReference"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct Package {
    pub elements: Vec<PackageElement, Kmalloc>,
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
#[derive(Debug, Clone)]
pub struct VarPackage {
    pub num_elements: Box<Evaluatable, Kmalloc>,
    pub initializer: Vec<PackageElement, Kmalloc>,
}

impl VarPackage {
    pub fn new(
        num_elements: Box<Evaluatable, Kmalloc>,
        initializer: Vec<PackageElement, Kmalloc>,
    ) -> Self {
        Self {
            num_elements,
            initializer,
        }
    }
}
