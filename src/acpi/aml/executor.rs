use core::{fmt::Debug, mem::MaybeUninit, ptr::NonNull};

use alloc::boxed::Box;

mod statements;

use crate::{
    acpi::aml::{
        Bytecode, Parser,
        evaluator::{
            Path,
            data::{DataRefObject, Integer},
        },
        namespace::{NameSpace, Object, objects::Method},
        opcode::Opcode,
        parser::term::TermArg,
    },
    kernel::memory::kmalloc::Kmalloc,
};

#[derive(Clone)]
pub struct Executable {
    bytecode: Box<[u8], Kmalloc>,
}

impl Debug for Executable {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "Executable")
    }
}

impl Executable {
    pub fn new(bytecode: &[u8]) -> Self {
        let dst = bytecode.to_vec_in(Kmalloc::default()).into_boxed_slice();
        Self { bytecode: dst }
    }
}

pub struct ExecuteContext<'a> {
    args: [Option<NonNull<NameSpace>>; 7],
    locals: [Option<NonNull<NameSpace>>; 8],
    pub parser: Parser<'a>,
    root: &'a NameSpace,
    current: &'a NameSpace,
    revision: Integer,
}

impl<'a> ExecuteContext<'a> {
    fn new(
        arguments: &[NonNull<NameSpace>],
        bytecode: Bytecode<'a>,
        root: &'a NameSpace,
        current: &'a NameSpace,
    ) -> Self {
        let mut args = [None; 7];
        for (i, arg) in arguments.iter().take(7).enumerate() {
            args[i] = Some(arg.clone());
        }

        let parser = Parser::from_context(bytecode, root, current);
        Self {
            args,
            locals: [None; 8],
            parser,
            root,
            current,
            revision: Integer::U32(1),
        }
    }

    pub const fn revision(&self) -> Integer {
        self.revision
    }

    pub const fn argument(&self, index: usize) -> Option<NonNull<NameSpace>> {
        if index < 7 { self.args[index] } else { None }
    }

    pub const fn local(&self, index: usize) -> Option<NonNull<NameSpace>> {
        if index < 8 { self.locals[index] } else { None }
    }

    pub fn get_namespace(&self, name: &Path) -> Option<&NameSpace> {
        let (mut current, iter) = match name {
            Path::Root(name) => (self.root, name.iter()),
            Path::Relative { level, path } => {
                let mut current = self.current;
                for _ in 0..*level {
                    current = current.parent()?;
                }
                (current, path.iter())
            }
        };

        for name in iter {
            current = current.get_by_path(&[name.as_ref()])?;
        }
        Some(current)
    }
}

pub struct Executor<'a> {
    _executable: &'a Executable,
    context: ExecuteContext<'a>,
}

impl<'a> Executor<'a> {
    pub fn new(
        _executable: &'a Executable,
        arguments: &[NonNull<NameSpace>],
        root: &'a NameSpace,
        current: &'a NameSpace,
    ) -> Self {
        let context = ExecuteContext::new(
            arguments,
            Bytecode::new(&_executable.bytecode),
            root,
            current,
        );
        Executor {
            _executable,
            context,
        }
    }

    pub fn call_method(
        &mut self,
        namespace: NonNull<NameSpace>,
        method: NonNull<Method>,
    ) -> Option<Option<DataRefObject>> {
        let method = unsafe { method.as_ref() };
        let executable = &method.executable;
        let arg_count = method.arg_count as usize;

        let mut arguments = [const { MaybeUninit::uninit() }; 7];
        for i in 0..arg_count {
            let arg = TermArg::parse(&mut self.context.parser);

            match arg {
                Ok(arg) => match arg {
                    TermArg::MethodInvocation((namespace, method)) => {
                        let return_value = self.call_method(namespace, method)??;
                        let return_value = match return_value {
                            DataRefObject::Reference(reference) => reference,
                            _ => return None,
                        };
                        arguments[i].write(return_value);
                    }
                    TermArg::Object(namespace) => {
                        arguments[i].write(namespace);
                    }
                    _ => {
                        unreachable!(
                            "Only MethodInvocation and Object are valid arguments for method calls"
                        );
                    }
                },
                Err(Some(_opcode)) => {
                    todo!();
                }
                Err(None) => {
                    return None;
                }
            }
        }
        let arguments = unsafe { arguments[..arg_count].assume_init_ref() };

        let current = unsafe { namespace.as_ref().parent()? };
        let mut executor = Executor::new(executable, arguments, self.context.root, current);
        executor.execute()
    }

    pub fn execute(&mut self) -> Option<Option<DataRefObject>> {
        loop {
            let parser = &mut self.context.parser;
            let opcode = Opcode::parse(&mut parser.bytecode).ok()?;
            match opcode {
                Opcode::Store => {
                    self.execute_store()?;
                }
                Opcode::Increment | Opcode::Decrement => {
                    self.execute_inc_dec(opcode)?;
                }
                Opcode::While => {
                    self.execute_while()?;
                }
                Opcode::Return => {
                    return self.execute_return();
                }
                Opcode::If => {
                    self.execute_if_else()?;
                }
                _ => return Some(None),
            }
        }
    }

    const fn bytecode(&mut self) -> &mut Bytecode<'a> {
        &mut self.context.parser.bytecode
    }
}

impl<'a> TryFrom<&'a NameSpace> for &'a Executable {
    type Error = ();

    fn try_from(value: &'a NameSpace) -> Result<Self, Self::Error> {
        match &value.object() {
            Object::Method(method) => Ok(&method.executable),
            _ => Err(()),
        }
    }
}
