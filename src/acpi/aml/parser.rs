use core::ops::{Deref, DerefMut};

use crate::acpi::aml::{Bytecode, namespace::NameSpace, parser::term::TermList};

pub(super) mod data;
mod namespace;
pub(super) mod namestring;
mod object;
pub(super) mod term;

pub struct Parser<'rootref> {
    pub bytecode: Bytecode<'rootref>,
    root: &'rootref NameSpace,
    current: &'rootref NameSpace,
}

impl<'rootref> Parser<'rootref> {
    pub fn new(bytecode: Bytecode<'static>, root: &'rootref NameSpace) -> Self {
        let current = root;
        Self {
            bytecode,
            root,
            current,
        }
    }

    pub fn from_context<'bc: 'rootref>(
        bytecode: Bytecode<'bc>,
        root: &'rootref NameSpace,
        current: &'rootref NameSpace,
    ) -> Self {
        Self {
            bytecode,
            root,
            current,
        }
    }

    pub fn parse(&mut self) -> Option<()> {
        let byte = self.bytecode.data.first()?;

        match byte {
            b'A'..b'Z' | b'^' | b'\\' | b'_' => panic!("AML starts with a namestring"),
            _ => {}
        }

        let _ = TermList::parse(self);

        Some(())
    }

    fn enter_namespace<'parser>(
        &'parser mut self,
        namespace: &'rootref NameSpace,
        length: usize,
    ) -> Option<ParserSlice<'parser, 'rootref>> {
        let slice_parser = Self {
            bytecode: self.bytecode.slice(length),
            root: self.root,
            current: namespace,
        };
        ParserSlice::new(self, slice_parser, length)
    }

    pub(super) fn slice(&mut self, length: usize) -> Option<ParserSlice<'_, 'rootref>> {
        let slice_parser = Self {
            bytecode: self.bytecode.slice(length),
            root: self.root,
            current: self.current,
        };
        ParserSlice::new(self, slice_parser, length)
    }
}

pub(super) struct ParserSlice<'parser, 'rootref> {
    _parser: &'parser mut Parser<'rootref>,
    slice: Parser<'rootref>,
}

impl<'parser, 'rootref> ParserSlice<'parser, 'rootref> {
    pub fn new(
        parser: &'parser mut Parser<'rootref>,
        slice: Parser<'rootref>,
        length: usize,
    ) -> Option<Self> {
        parser.bytecode.skip(length);

        Some(Self {
            _parser: parser,
            slice,
        })
    }
}

impl<'parser, 'rootref> Deref for ParserSlice<'parser, 'rootref> {
    type Target = Parser<'rootref>;

    fn deref(&self) -> &Self::Target {
        &self.slice
    }
}

impl<'parser, 'rootref> DerefMut for ParserSlice<'parser, 'rootref> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.slice
    }
}
