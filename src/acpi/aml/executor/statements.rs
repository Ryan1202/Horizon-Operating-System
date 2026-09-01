use core::{mem, ops::ControlFlow};

use crate::acpi::aml::{
    evaluator::{
        AsEvaluated, Evaluatable,
        data::{DataObject, DataRefObject, Integer},
        expressions::Expressions,
    },
    executor::{BreakKind, Executor},
    namespace::{Object, objects},
    opcode::Opcode,
    parser::{
        data::{PkgLength, SimpleName},
        term::TermArg,
    },
};

impl<'a> Executor<'a> {
    /// Store(source, dest) — 将 source 的值写入 dest 的 NameSpace 节点
    pub(super) fn execute_store(&mut self) -> Option<ControlFlow<BreakKind>> {
        // 1. 解析并求值源 TermArg
        let source = match Expressions::parse_termarg(&mut self.context)? {
            Ok(data) => data,
            Err(TermArg::MethodInvocation((namespace, method))) => {
                match self.call_method(namespace, method)? {
                    ControlFlow::Break(BreakKind::Return(value)) => Some(value),
                    flow @ ControlFlow::Break(_) => return Some(flow),
                    ControlFlow::Continue(()) => None,
                }
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

        // 3. 写入目标 Object
        let object = unsafe { dest.as_mut() };
        Expressions::store_to_object(source, object, &mut self.context);

        Some(ControlFlow::Continue(()))
    }

    /// Increment/Decrement — 读-改-写 SuperName
    pub(super) fn execute_inc_dec(&mut self, opcode: Opcode) -> Option<ControlFlow<BreakKind>> {
        let dest = SimpleName::parse(&mut self.context.parser, true)?;
        let mut dest = match dest {
            Ok(super_name) => super_name.evaluate(&mut self.context).ok()?,
            Err(_) => return None,
        };
        let object = unsafe { dest.as_mut() };
        if let Object::Data(objects::DataObject::Integer(integer)) = object {
            let val = Integer::U64(*integer);
            let result = match opcode {
                Opcode::Increment => val + Integer::U64(1),
                Opcode::Decrement => val - Integer::U64(1),
                _ => unreachable!(),
            };
            *integer = result.into();
        }
        Some(ControlFlow::Continue(()))
    }

    /// While(PkgLength, Predicate) — 循环直到谓词为0
    pub(super) fn execute_while(&mut self) -> Option<ControlFlow<BreakKind>> {
        let length = PkgLength::from_bytes(self.bytecode())?;
        let body_start = self.bytecode().current.as_ptr();
        let body_len = length.payload_length() as usize;

        loop {
            // 重置到循环体开头
            unsafe {
                self.bytecode().current =
                    core::slice::from_raw_parts(body_start, body_len);
            }

            // 求值谓词
            let predicate = Expressions::evaluate_integer(&mut self.context)?;
            if !predicate.as_bool() {
                break;
            }

            // 谓词消耗后的剩余部分作为执行体
            let exec_start = self.bytecode().current.as_ptr();
            let exec_len =
                body_len - unsafe { exec_start.byte_offset_from_unsigned(body_start) };

            let slice = self.bytecode().slice(exec_len);
            let old = mem::replace(self.bytecode(), slice);
            let result = self.execute();
            *self.bytecode() = old;

            match result {
                Some(ControlFlow::Break(BreakKind::Continue)) => continue,
                Some(ControlFlow::Break(BreakKind::Break)) => break,
                Some(ControlFlow::Break(BreakKind::Return(_))) => return result,
                None => return None,
                _ => {}
            }
        }

        // 跳过整个循环体
        let remaining = self.bytecode().current.len();
        self.bytecode().skip(body_len - (body_len - remaining));

        Some(ControlFlow::Continue(()))
    }

    pub(super) fn execute_return(&mut self) -> Option<ControlFlow<BreakKind>> {
        let arg = Expressions::parse_termarg(&mut self.context)?;

        match arg {
            Err(TermArg::MethodInvocation((namespace, method))) => {
                // 透传 Return，不包裹新的 BreakKind
                match self.call_method(namespace, method)? {
                    ControlFlow::Break(BreakKind::Return(value)) => {
                        Some(ControlFlow::Break(BreakKind::Return(value)))
                    }
                    flow => Some(flow),
                }
            }
            Err(TermArg::Object(_)) => {
                unreachable!("Object is not a valid return value for methods");
            }
            Err(arg) => {
                let eval = Evaluatable::from(arg);
                Some(ControlFlow::Break(BreakKind::Return(
                    eval.evaluate(&mut self.context).ok()?,
                )))
            }
            Ok(data) => Some(ControlFlow::Break(BreakKind::Return(data?))),
        }
    }

    /// Break — 跳出当前 While 循环
    pub(super) fn execute_break(&mut self) -> Option<ControlFlow<BreakKind>> {
        Some(ControlFlow::Break(BreakKind::Break))
    }

    /// Continue — 跳过循环体剩余，立即下一次迭代
    pub(super) fn execute_continue(&mut self) -> Option<ControlFlow<BreakKind>> {
        Some(ControlFlow::Break(BreakKind::Continue))
    }

    pub(super) fn execute_if_else(&mut self) -> Option<ControlFlow<BreakKind>> {
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
            Some(ControlFlow::Continue(()))
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

    /// Acquire(MutexObject, Timeout) — 获取 Mutex 锁
    pub(super) fn execute_acquire(&mut self) -> Option<ControlFlow<BreakKind>> {
        let super_name = SimpleName::parse(&mut self.context.parser, true)?.ok()?;
        let ns = super_name.evaluate(&mut self.context).ok()?;
        let timeout = self.bytecode().read_u16()?;
        let _ = timeout; // TODO: 支持超时

        if let Object::Mutex(mutex) = unsafe { ns.as_ref() } {
            mutex.lock();
        }
        Some(ControlFlow::Continue(()))
    }

    /// Release(MutexObject) — 释放 Mutex 锁
    pub(super) fn execute_release(&mut self) -> Option<ControlFlow<BreakKind>> {
        let super_name = SimpleName::parse(&mut self.context.parser, true)?.ok()?;
        let ns = super_name.evaluate(&mut self.context).ok()?;

        if let Object::Mutex(mutex) = unsafe { ns.as_ref() } {
            mutex.unlock();
        }
        Some(ControlFlow::Continue(()))
    }
}
