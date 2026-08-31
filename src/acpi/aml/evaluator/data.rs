use core::{
    ops::{Add, BitAnd, BitOr, BitXor, Div, Mul, Not, Sub},
    ptr::NonNull,
};

use alloc::{boxed::Box, vec::Vec};

use crate::{
    acpi::aml::{
        evaluator::AsEvaluated,
        executor::ExecuteContext,
        namespace::{NameSpace, data::PackageElement, objects},
    },
    kernel::memory::kmalloc::Kmalloc,
};

#[derive(Debug, Clone, Copy, PartialEq, PartialOrd)]
pub enum Integer {
    U32(u32),
    U64(u64),
}

impl Integer {
    pub const fn as_bool(&self) -> bool {
        match self {
            Integer::U32(n) => *n != 0,
            Integer::U64(n) => *n != 0,
        }
    }

    pub const fn bit_width(&self) -> usize {
        match self {
            Integer::U32(_) => 32,
            Integer::U64(_) => 64,
        }
    }

    /// 根据已有 Integer 的 variant 构造 bool → Integer
    pub fn from_bool_with(self, value: bool) -> Self {
        match self {
            Integer::U32(_) => Integer::U32(value as u32),
            Integer::U64(_) => Integer::U64(value as u64),
        }
    }
}

impl Into<u64> for Integer {
    fn into(self) -> u64 {
        match self {
            Integer::U32(n) => n as u64,
            Integer::U64(n) => n,
        }
    }
}

impl Add for Integer {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a.wrapping_add(b)),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a.wrapping_add(b)),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl Sub for Integer {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a.wrapping_sub(b)),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a.wrapping_sub(b)),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl Mul for Integer {
    type Output = Self;

    fn mul(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a.wrapping_mul(b)),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a.wrapping_mul(b)),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl Div for Integer {
    type Output = Self;

    fn div(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a.wrapping_div(b)),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a.wrapping_div(b)),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl BitAnd for Integer {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a & b),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a & b),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl BitOr for Integer {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a | b),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a | b),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl BitXor for Integer {
    type Output = Self;

    fn bitxor(self, rhs: Self) -> Self::Output {
        match (self, rhs) {
            (Integer::U32(a), Integer::U32(b)) => Integer::U32(a ^ b),
            (Integer::U64(a), Integer::U64(b)) => Integer::U64(a ^ b),
            _ => panic!("Mismatched integer types"),
        }
    }
}

impl Not for Integer {
    type Output = Self;

    fn not(self) -> Self::Output {
        match self {
            Integer::U32(a) => Integer::U32(!a),
            Integer::U64(a) => Integer::U64(!a),
        }
    }
}

#[derive(Debug)]
pub enum DataObject {
    Integer(Integer),
    String(Box<[i8], Kmalloc>),
    Buffer(Box<[u8], Kmalloc>),
    Package(Package),
}

impl DataObject {
    pub fn evaluate(data: objects::DataObject, context: &mut ExecuteContext) -> Result<Self, ()> {
        match data {
            objects::DataObject::Integer(int) => Ok(match context.revision() {
                Integer::U32(_) => DataObject::Integer(Integer::U32(int as u32)),
                Integer::U64(_) => DataObject::Integer(Integer::U64(int)),
            }),
            objects::DataObject::String(str) => Ok(DataObject::String(str)),
            objects::DataObject::Buffer(buf) => Ok(DataObject::Buffer(buf)),
            objects::DataObject::Package(pkg) => {
                let vec = Self::evaluate_package(pkg.elements.len(), pkg.elements, context)?;

                Ok(DataObject::Package(Package { elements: vec }))
            }
            objects::DataObject::Revision => Ok(DataObject::Integer(context.revision())),
            objects::DataObject::VarPackage(pkg) => {
                let num_elements = pkg.num_elements.evaluate(context).unwrap();
                let num_elements = match num_elements {
                    Integer::U32(n) => n as usize,
                    Integer::U64(n) => n as usize,
                };

                let vec = Self::evaluate_package(num_elements, pkg.initializer, context)?;

                Ok(DataObject::Package(Package { elements: vec }))
            }
        }
    }

    fn evaluate_package(
        num_elements: usize,
        elements: Vec<PackageElement, Kmalloc>,
        context: &mut ExecuteContext,
    ) -> Result<Vec<DataRefObject, Kmalloc>, ()> {
        let mut vec = Vec::with_capacity_in(num_elements, Kmalloc::default());
        for element in elements {
            match element {
                PackageElement::DataObject(data) => {
                    vec.push_within_capacity(DataRefObject::DataObject(Self::evaluate(
                        data, context,
                    )?))
                    .unwrap();
                }
                PackageElement::NameSpaceReference(object) => {
                    vec.push_within_capacity(DataRefObject::Reference(object))
                        .unwrap();
                }
                PackageElement::ObjectReference(eval) => {
                    vec.push_within_capacity(DataRefObject::Reference(eval.evaluate(context)?))
                        .unwrap();
                }
            }
        }
        Ok(vec)
    }
}

#[derive(Debug)]
pub enum DataRefObject {
    DataObject(DataObject),
    Reference(NonNull<NameSpace>),
}

#[derive(Debug)]
pub struct Package {
    pub elements: Vec<DataRefObject, Kmalloc>,
}
