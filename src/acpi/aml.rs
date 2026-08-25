pub mod executor;
pub mod namespace;
mod parser;

pub use parser::Parser;

#[derive(Clone, Default)]
pub struct Bytecode {
    data: &'static [u8],
    current: &'static [u8],
}

impl Bytecode {
    pub fn from_bytes(data: &'static [u8]) -> Bytecode {
        Bytecode {
            data,
            current: data,
        }
    }

    pub fn read(&mut self, size: usize) -> &'static [u8] {
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

    pub fn skip(&mut self, count: usize) {
        self.current = &self.current[count..];
    }

    pub fn slice(&self, size: usize) -> Bytecode {
        let current = &self.current[..size];
        Bytecode {
            data: current,
            current,
        }
    }
}
