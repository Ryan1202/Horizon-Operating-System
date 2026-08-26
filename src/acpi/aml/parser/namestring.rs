use core::slice::Iter;

use alloc::boxed::Box;

use crate::{
    acpi::aml::{Bytecode, namespace, parser::prefix},
    kernel::memory::kmalloc::Kmalloc,
};

#[derive(Clone)]
pub(in crate::acpi) struct NamePath(pub &'static [[u8; 4]]);

impl NamePath {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let first = bytecode.first()?;

        match first {
            0x00 => {
                bytecode.next()?;
                Some(Self(&[]))
            }
            prefix::DUAL_NAME_PREFIX => {
                let _ = bytecode.next();

                Some(Self(unsafe { bytecode.read(8).as_chunks_unchecked() }))
            }
            prefix::MULTI_NAME_PREFIX => {
                let _ = bytecode.next();
                let length = bytecode.next()?;

                Some(Self(unsafe {
                    bytecode.read(length as usize * 4).as_chunks_unchecked()
                }))
            }
            b'A'..=b'Z' | b'_' => Some(Self(unsafe { bytecode.read(4).as_chunks_unchecked() })),
            _ => None,
        }
    }

    pub const fn count(&self) -> usize {
        self.0.len()
    }

    pub fn last_name(&self) -> Option<&[u8; 4]> {
        self.0.iter().last()
    }

    pub const fn bytecode_length(&self) -> usize {
        match self.count() {
            0 => 1,
            1 => 4,
            2 => 9,
            n => 2 + n * 4,
        }
    }

    pub fn to_boxed(self) -> Box<[namespace::Name], Kmalloc> {
        let len = self.0.len();

        let mut boxed = Box::new_uninit_slice_in(len, Kmalloc::default());
        for (i, name) in self.0.iter().enumerate() {
            boxed[i].write(namespace::Name::new(*name));
        }
        unsafe { boxed.assume_init() }
    }
}

impl<'a> IntoIterator for &'a NamePath {
    type Item = &'a [u8; 4];
    type IntoIter = Iter<'a, [u8; 4]>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

#[derive(Clone)]
pub(in crate::acpi) enum Namestring {
    Root(NamePath),
    Relative { level: u8, path: NamePath },
}

impl Namestring {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let mut level = 0;
        while let Some(b'^') = bytecode.first() {
            level += 1;
            bytecode.next();
        }

        let first_byte = bytecode.first()?;

        match first_byte {
            b'\\' => {
                let _ = bytecode.next();
                Some(Namestring::Root(NamePath::from_bytes(bytecode)?))
            }
            _ => NamePath::from_bytes(bytecode).map(|path| Namestring::Relative { level, path }),
        }
    }

    pub fn last_name(&self) -> Option<&[u8; 4]> {
        match self {
            Namestring::Root(path) => path.last_name(),
            Namestring::Relative { path, .. } => path.last_name(),
        }
    }

    pub const fn bytecode_length(&self) -> usize {
        match self {
            Namestring::Root(path) => 1 + path.bytecode_length(),
            &Namestring::Relative { level, ref path } => level as usize + path.bytecode_length(),
        }
    }
}

impl<'a> IntoIterator for &'a Namestring {
    type Item = NamestringIterItem<'a>;
    type IntoIter = NamestringIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        NamestringIter {
            namestring: self,
            index: 0,
        }
    }
}

pub(in crate::acpi) struct NamestringIter<'a> {
    namestring: &'a Namestring,
    index: usize,
}

pub(in crate::acpi) enum NamestringIterItem<'a> {
    Root,
    Parent,
    Path { path: &'a NamePath },
}

impl<'a> Iterator for NamestringIter<'a> {
    type Item = NamestringIterItem<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        match self.namestring {
            Namestring::Root(path) => {
                if self.index == 0 {
                    self.index += 1;
                    Some(NamestringIterItem::Root)
                } else if self.index == 1 {
                    self.index += 1;
                    Some(NamestringIterItem::Path { path })
                } else {
                    None
                }
            }
            &Namestring::Relative { level, ref path } => {
                if self.index < level as usize {
                    let item = NamestringIterItem::Parent;
                    self.index += 1;
                    Some(item)
                } else if self.index == level as usize {
                    self.index += 1;
                    Some(NamestringIterItem::Path { path })
                } else {
                    None
                }
            }
        }
    }
}
