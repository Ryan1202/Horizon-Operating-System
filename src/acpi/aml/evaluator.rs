use core::ptr::NonNull;

use alloc::boxed::Box;

use crate::{
    acpi::aml::{
        evaluator::data::{DataObject, DataRefObject, Integer},
        executor::ExecuteContext,
        namespace::{Name, NameSpace, Object, data::PackageElement, objects},
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub mod data;
pub mod expressions;
mod region;

#[derive(Debug, Clone)]
pub enum Evaluatable {
    Reference(ReferenceType),
    Builtin(BuiltinObject),
    DataObject(objects::DataObject),
    MethodInvocation((NonNull<NameSpace>, NonNull<objects::Method>)),
    NamedField((NonNull<NameSpace>, NonNull<objects::FieldUnit>)),
}

pub trait AsEvaluated<T: Sized>: Sized {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<T, ()>;
}

impl AsEvaluated<DataObject> for Evaluatable {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<DataObject, ()> {
        match self {
            Evaluatable::DataObject(obj) => DataObject::evaluate(obj, context),
            _ => Err(()),
        }
    }
}

impl AsEvaluated<Integer> for Evaluatable {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<Integer, ()> {
        match self {
            Evaluatable::DataObject(objects::DataObject::Integer(int)) => {
                match context.revision() {
                    Integer::U32(_) => Ok(Integer::U32(int as u32)),
                    Integer::U64(_) => Ok(Integer::U64(int)),
                }
            }
            Evaluatable::Builtin(BuiltinObject::Arg(arg)) => context
                .argument(arg as usize)
                .ok_or(())
                .and_then(|object| Self::evaluate_integer(object, context)),
            Evaluatable::Builtin(BuiltinObject::Local(local)) => context
                .local(local as usize)
                .ok_or(())
                .and_then(|object| Self::evaluate_integer(object, context)),
            _ => Err(()),
        }
    }
}

impl Evaluatable {
    fn evaluate_integer(
        object: NonNull<Object>,
        context: &mut ExecuteContext,
    ) -> Result<Integer, ()> {
        let object = unsafe { object.as_ref() };

        match object {
            Object::Data(objects::DataObject::Integer(int)) => match context.revision() {
                Integer::U32(_) => Ok(Integer::U32(*int as u32)),
                Integer::U64(_) => Ok(Integer::U64(*int)),
            },
            Object::FieldUnit(field) => field.clone().evaluate(context),
            _ => Err(()),
        }
    }
}

impl AsEvaluated<DataRefObject> for Evaluatable {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<DataRefObject, ()> {
        match self {
            Self::DataObject(obj) => Ok(DataRefObject::DataObject(DataObject::evaluate(
                obj, context,
            )?)),
            Self::Reference(reference) => {
                let evaluated = reference.evaluate(context)?;
                Ok(DataRefObject::Reference(evaluated))
            }
            Self::Builtin(builtin) => builtin.evaluate(context).map(DataRefObject::Reference),
            Self::MethodInvocation(_) => todo!(),
            Self::NamedField(_) => todo!(),
        }
    }
}

impl AsEvaluated<NonNull<Object>> for Evaluatable {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<NonNull<Object>, ()> {
        match self {
            Evaluatable::Reference(reference) => reference.evaluate(context),
            Evaluatable::Builtin(builtin) => builtin.evaluate(context),
            _ => Err(()),
        }
    }
}

impl AsEvaluated<NonNull<Object>> for ReferenceType {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<NonNull<Object>, ()> {
        match self {
            ReferenceType::RefOf(name) => name.evaluate(context),
            ReferenceType::DerefOf(inner) => {
                let object = inner.evaluate(context)?;
                match unsafe { object.as_ref() } {
                    Object::ObjectReference(evaluatable) => evaluatable.clone().evaluate(context),
                    _ => Ok(object),
                }
            }
            ReferenceType::IndexOf(index_of) => {
                let source = index_of.source.evaluate(context)?;
                let idx = index_of.index.evaluate(context)?;
                let index = match idx {
                    Integer::U32(i) => i as usize,
                    Integer::U64(i) => i as usize,
                };
                match source {
                    DataRefObject::DataObject(DataObject::Package(package)) => {
                        match package.elements.into_iter().nth(index) {
                            Some(DataRefObject::Reference(object)) => Ok(object),
                            _ => Err(()),
                        }
                    }
                    DataRefObject::Reference(object) => match unsafe { object.as_ref() } {
                        Object::Data(objects::DataObject::Package(package)) => {
                            match package.elements.iter().nth(index) {
                                Some(PackageElement::DirectReference(r#ref)) => Ok(*r#ref),
                                _ => Err(()),
                            }
                        }
                        _ => Err(()),
                    },
                    _ => Err(()),
                }
            }
            _ => Err(()),
        }
    }
}

impl AsEvaluated<NonNull<Object>> for SuperName {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<NonNull<Object>, ()> {
        match self {
            SuperName::Name(path) => {
                let ns = context.get_namespace(&path).ok_or(())?;
                Ok(NonNull::from_ref(ns.object()))
            }
            SuperName::Builtin(builtin) => builtin.evaluate(context),
            SuperName::Nested(nested) => nested.evaluate(context),
        }
    }
}

impl AsEvaluated<NonNull<Object>> for BuiltinObject {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<NonNull<Object>, ()> {
        match self {
            BuiltinObject::Arg(arg) => context.argument(arg as usize).ok_or(()),
            BuiltinObject::Local(local) => context.local(local as usize).ok_or(()),
            BuiltinObject::Debug => unimplemented!(),
        }
    }
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
