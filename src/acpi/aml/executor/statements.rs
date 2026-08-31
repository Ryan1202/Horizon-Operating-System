use core::mem;

use crate::acpi::aml::{
    evaluator::{AsEvaluated, Evaluatable, data::DataRefObject, expressions::Expressions},
    executor::Executor,
    opcode::Opcode,
    parser::{
        data::{PkgLength, SimpleName},
        term::TermArg,
    },
};

impl<'a> Executor<'a> {
    /// Store(source, dest) — 将 source 的值写入 dest 的 NameSpace 节点
    pub(super) fn execute_store(&mut self) -> Option<Option<DataRefObject>> {
        // 1. 解析并求值源 TermArg
        let source = match Expressions::parse_termarg(&mut self.context)? {
            Ok(data) => data,
            Err(TermArg::MethodInvocation((namespace, method))) => {
                self.call_method(namespace, method)?
            }
            Err(arg) => {
                let eval = Evaluatable::from(arg);
                eval.evaluate(&mut self.context).ok()
            }
        };

        let source = source?;

        // 2. 解析目标 SuperName
        let dest = SimpleName::parse(&mut self.context.parser, true)?;
        let mut dest = match dest {
            Ok(super_name) => super_name.evaluate(&mut self.context).ok()?,
            Err(_) => return None,
        };

        // 3. 写入目标 NameSpace 节点
        unsafe { dest.as_mut() }.with_object(|object| {
            Expressions::store_to_object(source, object);
        });

        Some(None)
    }

    pub(super) fn execute_return(&mut self) -> Option<Option<DataRefObject>> {
        let arg = Expressions::parse_termarg(&mut self.context)?;

        match arg {
            Err(TermArg::MethodInvocation((namespace, method))) => {
                self.call_method(namespace, method)
            }
            Err(TermArg::Object(_)) => {
                unreachable!("Object is not a valid return value for methods");
            }
            Err(arg) => {
                let eval = Evaluatable::from(arg);
                Some(eval.evaluate(&mut self.context).ok())
            }
            Ok(data) => Some(data),
        }
    }

    pub(super) fn execute_if_else(&mut self) -> Option<Option<DataRefObject>> {
        let length = PkgLength::from_bytes(self.bytecode())?;

        let start = self.bytecode().current.as_ptr();
        let predicate = Expressions::evaluate_integer(&mut self.context)?;
        let end = self.bytecode().current.as_ptr();

        let slice_length =
            length.payload_length() as usize - unsafe { end.byte_offset_from_unsigned(start) };

        let then = if predicate.as_bool() {
            let slice = self.bytecode().slice(slice_length);
            let old = mem::replace(self.bytecode(), slice);

            let result = self.execute();

            *self.bytecode() = old;
            result
        } else {
            Some(None)
        };
        self.bytecode().skip(slice_length);
        let bytecode = self.bytecode();

        if bytecode.current.is_empty() {
            return then;
        }

        let mut _bc = bytecode.clone();
        let opcode = Opcode::parse(&mut _bc).ok()?;

        if let Opcode::Else = opcode {
            *bytecode = _bc;

            let length = PkgLength::from_bytes(bytecode)?;
            let slice_length = length.payload_length() as usize;

            let result = if predicate.as_bool() {
                then
            } else {
                let slice = bytecode.slice(slice_length);
                let old = mem::replace(bytecode, slice);

                let r#else = self.execute();

                *self.bytecode() = old;

                r#else
            };

            self.bytecode().skip(slice_length);
            result
        } else {
            then
        }
    }
}
