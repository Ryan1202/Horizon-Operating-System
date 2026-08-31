pub mod evaluator;
pub mod executor;
pub mod namespace;
mod opcode;
mod parser;

use core::array;

pub use parser::Parser;

#[derive(Clone, Default)]
pub struct Bytecode<'a> {
    data: &'a [u8],
    current: &'a [u8],
}

impl<'a> Bytecode<'a> {
    pub fn new(data: &'a [u8]) -> Bytecode<'a> {
        Bytecode {
            data,
            current: data,
        }
    }

    pub fn read(&mut self, size: usize) -> &'a [u8] {
        let (left, right) = self.current.split_at(size);
        self.current = right;
        left
    }

    pub fn first(&self) -> Option<u8> {
        self.current.first().copied()
    }

    pub fn next(&mut self) -> Option<u8> {
        let (&byte, rest) = self.current.split_first()?;
        self.current = rest;
        Some(byte)
    }

    pub fn read_u16(&mut self) -> Option<u16> {
        let bytes = self.read(2);
        Some(u16::from_le_bytes([bytes[0], bytes[1]]))
    }

    pub fn read_u32(&mut self) -> Option<u32> {
        let bytes = self.read(4);
        Some(u32::from_le_bytes(array::from_fn(|i| bytes[i])))
    }

    pub fn read_u64(&mut self) -> Option<u64> {
        let bytes = self.read(8);
        Some(u64::from_le_bytes(array::from_fn(|i| bytes[i])))
    }

    pub fn skip(&mut self, count: usize) {
        self.current = &self.current[count..];
    }

    pub fn slice(&mut self, size: usize) -> Self {
        let current = &self.current[..size];
        Bytecode {
            data: current,
            current,
        }
    }
}
