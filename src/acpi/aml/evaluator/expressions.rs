use alloc::vec::Vec;

use crate::{
    acpi::aml::{
        evaluator::{
            AsEvaluated, Evaluatable, ReferenceType,
            data::{self, DataObject, DataRefObject, Integer},
            region,
        },
        executor::ExecuteContext,
        namespace::{
            Object,
            data::{Package, PackageElement},
            objects,
        },
        opcode::Opcode,
        parser::{data::Reference, term::TermArg},
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub(in crate::acpi) struct Expressions;

pub(in crate::acpi) type TermArgResult = Result<Option<DataRefObject>, TermArg>;

impl Expressions {
    pub(in crate::acpi) fn parse_termarg(context: &mut ExecuteContext) -> Option<TermArgResult> {
        let arg = TermArg::parse(&mut context.parser);

        match arg {
            Ok(arg) => Some(Err(arg)),
            Err(Some(opcode)) => Expressions::evaluate_expression(opcode, context)
                .map(Some)
                .map(Ok),
            Err(None) => Some(Ok(None)),
        }
    }

    pub(in crate::acpi) fn evaluate_integer(context: &mut ExecuteContext) -> Option<Integer> {
        let arg = Self::parse_termarg(context)?;

        match arg {
            Err(TermArg::MethodInvocation(_)) => None,
            Err(TermArg::Object(_)) => None,
            Err(arg) => {
                let eval = Evaluatable::from(arg);
                eval.evaluate(context).ok()
            }
            Ok(Some(DataRefObject::DataObject(DataObject::Integer(int)))) => Some(int),
            Ok(Some(DataRefObject::Reference(ns))) => {
                let object = unsafe { ns.as_ref() }.object();
                if let Object::Data(objects::DataObject::Integer(integer)) = object {
                    match context.revision() {
                        Integer::U32(_) => Some(Integer::U32(*integer as u32)),
                        Integer::U64(_) => Some(Integer::U64(*integer)),
                    }
                } else {
                    None
                }
            }
            _ => None,
        }
    }

    /// 将 DataRefObject 写入目标 Object。匹配时原地更新，不匹配时替换。
    pub(in crate::acpi) fn store_to_object(
        source: DataRefObject,
        target: &mut Object,
        context: &mut ExecuteContext,
    ) {
        type Source = DataObject;
        type Target = objects::DataObject;
        match source {
            DataRefObject::DataObject(data) => match (target, data) {
                (Object::Data(Target::Integer(t)), Source::Integer(s)) => {
                    *t = s.into();
                }
                (Object::Data(Target::String(t)), Source::String(s)) => {
                    *t = s;
                }
                (Object::Data(Target::Buffer(t)), Source::Buffer(s)) => {
                    *t = s;
                }
                (Object::Data(Target::Package(t)), Source::Package(s)) => {
                    Self::store_package_inplace(t, s);
                }
                (Object::FieldUnit(field), Source::Integer(int)) => {
                    region::write_field_unit(field, int, context);
                }
                (target, data) => {
                    *target = Object::Data(Self::data_to_objects(data));
                }
            },
            DataRefObject::Reference(namespace) => {
                let obj = unsafe { namespace.as_ref() }.object();
                if let (Object::FieldUnit(field), Object::Data(objects::DataObject::Integer(int))) =
                    (&mut *target, obj)
                {
                    region::write_field_unit(field, Integer::U64(*int), context);
                } else {
                    *target = obj.clone();
                }
            }
        }
    }

    fn store_package_inplace(target: &mut Package, source: data::Package) {
        target.elements.clear();
        for e in source.elements {
            target.elements.push(Self::dataref_to_element(e));
        }
    }

    fn data_to_objects(data: DataObject) -> objects::DataObject {
        type Source = DataObject;
        type Target = objects::DataObject;
        match data {
            Source::Integer(int) => Target::Integer(int.into()),
            Source::String(str) => Target::String(str),
            Source::Buffer(buf) => Target::Buffer(buf),
            Source::Package(pkg) => {
                let mut elements = Vec::new_in(Kmalloc::default());
                for e in pkg.elements {
                    elements.push(Self::dataref_to_element(e));
                }
                Target::Package(Package::new(elements))
            }
        }
    }

    fn dataref_to_element(e: DataRefObject) -> PackageElement {
        match e {
            DataRefObject::DataObject(d) => PackageElement::DataObject(Self::data_to_objects(d)),
            DataRefObject::Reference(ns) => PackageElement::NameSpaceReference(ns),
        }
    }

    pub fn evaluate_expression(
        opcode: Opcode,
        context: &mut ExecuteContext,
    ) -> Option<DataRefObject> {
        match opcode {
            Opcode::And => Self::binary_op(context, |a, b| a & b),
            Opcode::Or => Self::binary_op(context, |a, b| a | b),
            Opcode::Add => Self::binary_op(context, |a, b| a + b),
            Opcode::Subtract => Self::binary_op(context, |a, b| a - b),
            Opcode::Multiply => Self::binary_op(context, |a, b| a * b),
            Opcode::Mod => Self::binary_op(context, |a, b| a % b),
            Opcode::LEqual => Self::binary_cmp(context, |a, b| a == b),
            Opcode::LGreater => Self::binary_cmp(context, |a, b| a > b),
            Opcode::LLess => Self::binary_cmp(context, |a, b| a < b),
            Opcode::LNotEqual => Self::binary_cmp(context, |a, b| a != b),
            Opcode::LLessEqual => Self::binary_cmp(context, |a, b| a <= b),
            Opcode::LGreaterEqual => Self::binary_cmp(context, |a, b| a >= b),
            Opcode::ShiftLeft => Self::binary_op(context, |a, b| a << b),
            Opcode::ShiftRight => Self::binary_op(context, |a, b| a >> b),
            Opcode::Not => {
                let a = Self::evaluate_integer(context)?;
                let target =
                    Reference::parse_target(&mut context.parser)?.map(ReferenceType::RefOf);
                let result = !a;
                Self::write_target(target, result, context);
                Some(DataRefObject::DataObject(DataObject::Integer(result)))
            }
            Opcode::Divide => {
                let a = Self::evaluate_integer(context)?;
                let b = Self::evaluate_integer(context)?;
                let remainder_target =
                    Reference::parse_target(&mut context.parser)?.map(ReferenceType::RefOf);
                let result_target =
                    Reference::parse_target(&mut context.parser)?.map(ReferenceType::RefOf);

                let quotient = a / b;
                let remainder = a - quotient * b;

                Self::write_target(remainder_target, remainder, context);
                Self::write_target(result_target, quotient, context);

                Some(DataRefObject::DataObject(DataObject::Integer(quotient)))
            }
            _ => todo!(),
        }
    }

    fn binary_op(
        context: &mut ExecuteContext,
        op: impl Fn(Integer, Integer) -> Integer,
    ) -> Option<DataRefObject> {
        let a = Self::evaluate_integer(context)?;
        let b = Self::evaluate_integer(context)?;
        let target = Reference::parse_target(&mut context.parser)?.map(ReferenceType::RefOf);

        let result = op(a, b);
        Self::write_target(target, result, context);

        Some(DataRefObject::DataObject(DataObject::Integer(result)))
    }

    fn binary_cmp(
        context: &mut ExecuteContext,
        cmp: impl Fn(Integer, Integer) -> bool,
    ) -> Option<DataRefObject> {
        let a = Self::evaluate_integer(context)?;
        let b = Self::evaluate_integer(context)?;
        let result = a.from_bool_with(cmp(a, b));
        Some(DataRefObject::DataObject(DataObject::Integer(result)))
    }

    fn write_target(target: Option<ReferenceType>, value: Integer, context: &mut ExecuteContext) {
        let Some(target) = target else { return };
        let mut ns = match target.evaluate(context) {
            Ok(ns) => ns,
            Err(_) => return,
        };
        unsafe { ns.as_mut() }.with_object(|object| {
            if let Object::Data(objects::DataObject::Integer(integer)) = object {
                *integer = value.into();
            }
        });
    }
}
