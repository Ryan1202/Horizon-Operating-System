use crate::acpi::aml::{Bytecode, parser::prefix};

pub(in crate::acpi) enum NamePath {
    Null,
    Single(&'static [u8]),
    Dual(&'static [u8]),
    Multi(&'static [u8]),
}

impl NamePath {
    pub fn from_bytes(bytecode: &mut Bytecode) -> Option<Self> {
        let first = bytecode.first()?;

        match first {
            0x00 => {
                bytecode.next()?;
                Some(Self::Null)
            }
            prefix::DUAL_NAME_PREFIX => {
                let _ = bytecode.next();

                Some(Self::Dual(bytecode.read(8)))
            }
            prefix::MULTI_NAME_PREFIX => {
                let _ = bytecode.next();
                let length = bytecode.next()?;

                Some(Self::Multi(bytecode.read(length as usize * 4)))
            }
            b'A'..=b'Z' | b'_' => Some(Self::Single(bytecode.read(4))),
            _ => None,
        }
    }

    pub const fn count(&self) -> usize {
        match self {
            NamePath::Null => 0,
            NamePath::Single(_) => 1,
            NamePath::Dual(_) => 2,
            NamePath::Multi(names) => names.len() / 4,
        }
    }

    pub fn last_name(&self) -> Option<&[u8]> {
        match self {
            NamePath::Null => None,
            NamePath::Single(name) => Some(name),
            NamePath::Dual(names) => Some(&names[4..8]),
            NamePath::Multi(names) => {
                let left = names.len() - 4;
                Some(&names[left..])
            }
        }
    }

    pub const fn bytecode_length(&self) -> usize {
        match self {
            NamePath::Null => 1,
            NamePath::Single(_) => 4,
            NamePath::Dual(_) => 9,
            NamePath::Multi(names) => 2 + names.len(),
        }
    }
}

impl<'a> IntoIterator for &'a NamePath {
    type Item = &'a [u8];
    type IntoIter = NamePathIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        NamePathIter {
            path: self,
            index: 0,
        }
    }
}

pub(in crate::acpi) struct NamePathIter<'a> {
    path: &'a NamePath,
    index: usize,
}

impl<'a> Iterator for NamePathIter<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        match self.path {
            NamePath::Null => None,
            NamePath::Single(name) => {
                if self.index == 0 {
                    self.index += 1;
                    Some(name)
                } else {
                    None
                }
            }
            NamePath::Dual(names) => {
                if self.index < 2 {
                    let name = &names[self.index * 4..(self.index + 1) * 4];
                    self.index += 1;
                    Some(name)
                } else {
                    None
                }
            }
            NamePath::Multi(names) => {
                let length = names.len() / 4;
                if self.index < length {
                    let name = &names[self.index * 4..(self.index + 1) * 4];
                    self.index += 1;
                    Some(name)
                } else {
                    None
                }
            }
        }
    }
}

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

    pub fn last_name(&self) -> Option<&[u8]> {
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
