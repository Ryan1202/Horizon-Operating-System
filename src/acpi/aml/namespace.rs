use core::{
    field::field_of,
    fmt::{self, Write},
    pin::Pin,
    ptr::NonNull,
};

pub mod data;
pub mod objects;

pub use objects::Object;

use alloc::boxed::Box;

use crate::{
    acpi::aml::parser::namestring::{Namestring, NamestringIterItem},
    kernel::memory::kmalloc::Kmalloc,
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::{SpinGuard, Spinlock},
    },
    printk,
};

pub static NAMESPACE_ROOT: Spinlock<NameSpace> = Spinlock::new(NameSpace::new_uninit(
    Name::new([b'\\', 0, 0, 0]),
    Object::Scope,
));

#[derive(Clone)]
pub struct Name([u8; 4]);

impl Name {
    pub const fn new(bytes: [u8; 4]) -> Self {
        Self(bytes)
    }
}

impl fmt::Debug for Name {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for byte in self.0 {
            f.write_char(char::from(byte))?;
        }
        Ok(())
    }
}

impl From<&[u8]> for Name {
    fn from(slice: &[u8]) -> Self {
        let mut bytes = [0u8; 4];

        for (i, &b) in slice.iter().enumerate().take(4) {
            bytes[i] = b;
        }

        Self(bytes)
    }
}

impl AsRef<[u8]> for Name {
    fn as_ref(&self) -> &[u8] {
        self.0.as_slice()
    }
}

pub struct NameSpace {
    name: Name,
    object: Object,

    list_node: ListNode<NameSpace>,
    children: Spinlock<ListHead<field_of!(NameSpace, list_node)>>,
    parent: Option<NonNull<NameSpace>>,
}

impl NameSpace {
    pub const fn new_uninit(name: Name, object: Object) -> Self {
        Self {
            name,
            object,
            list_node: ListNode::new(),
            children: Spinlock::new(ListHead::default()),
            parent: None,
        }
    }

    pub fn init(&mut self) {
        unsafe {
            self.children.init_with(|head| head.init());
        };
    }

    pub fn add_child(&self, child: &mut NameSpace) {
        child.parent = Some(NonNull::from_ref(self));
        let mut guard = self.children_locked();
        unsafe { guard.as_mut().get_unchecked_mut() }.add_tail(child);
    }

    pub fn root() -> Pin<&'static Spinlock<NameSpace>> {
        unsafe { Pin::new_unchecked(&NAMESPACE_ROOT) }
    }

    pub fn name(&self) -> &Name {
        &self.name
    }

    pub fn parent(&self) -> Option<&NameSpace> {
        unsafe { Some(self.parent?.as_ref()) }
    }

    pub fn print_tree(&self) {
        self.print_tree_at(0);
    }

    fn print_tree_at(&self, depth: usize) {
        printk!(
            "{:indent$}{:?} {:?}\n",
            "",
            self.name,
            self.object,
            indent = depth * 2
        );

        let mut guard = self.children_locked();
        for child in unsafe { guard.as_mut().get_unchecked_mut().iter() } {
            unsafe { child.as_ref() }.print_tree_at(depth + 1);
        }
    }

    pub fn select_child_by_name(&self, name: &[u8]) -> Option<&NameSpace> {
        let mut guard = self.children_locked();
        unsafe { guard.as_mut().get_unchecked_mut().iter() }
            .find(|child: &NonNull<Self>| unsafe { child.as_ref() }.name().as_ref() == name)
            .map(|child| unsafe { child.as_ref() })
    }

    fn children_pinned(&self) -> Pin<&Spinlock<ListHead<field_of!(NameSpace, list_node)>>> {
        unsafe { Pin::new_unchecked(&self.children) }
    }

    pub fn children_locked(
        &self,
    ) -> SpinGuard<'_, Pin<&mut ListHead<field_of!(NameSpace, list_node)>>> {
        self.children_pinned().lock_pinned()
    }
}

impl NameSpace {
    fn _get<'a, 'name>(
        &'a self,
        root: &'a NameSpace,
        namestring: &'name Namestring,
    ) -> Option<(&'a NameSpace, Option<&'name [u8]>)> {
        let mut current = self;

        for item in namestring {
            match item {
                NamestringIterItem::Root => {
                    current = root;
                }
                NamestringIterItem::Parent => {
                    current = current.parent()?;
                }
                NamestringIterItem::Path { path } => {
                    let count = path.count();
                    for (i, name) in path.into_iter().enumerate() {
                        if let Some(child) = current.select_child_by_name(name) {
                            current = child;
                        } else if i == count - 1 {
                            return Some((current, Some(namestring.last_name()?)));
                        } else {
                            return None;
                        }
                    }
                }
            }
        }

        Some((current, None))
    }

    pub(super) fn get<'a, 'name>(
        &'a self,
        root: &'a NameSpace,
        namestring: &'name Namestring,
    ) -> Option<(&'a NameSpace, Option<&'name [u8]>)> {
        let (current, last_name) = self._get(root, namestring)?;
        if last_name.is_some() {
            None
        } else {
            Some((current, last_name))
        }
    }

    pub(super) fn get_or_insert<'a, 'name>(
        &'a self,
        root: &'a NameSpace,
        namestring: &'name Namestring,
        object: Object,
    ) -> Option<&'a NameSpace> {
        let (mut current, last_name) = self._get(root, namestring)?;
        if let Some(last_name) = last_name {
            let child = NameSpace::new_uninit(Name::from(last_name), object);
            let mut child = Box::<_, Kmalloc>::new_in(child, Kmalloc::default());
            child.init();
            current.add_child(&mut child);
            current = Box::leak(child);
        }
        Some(current)
    }
}

pub fn init_namespace() {
    unsafe {
        NAMESPACE_ROOT.init_with(|root| {
            root.init();
        })
    };
}

#[unsafe(no_mangle)]
pub extern "C" fn acpi_print_namespace() {
    NameSpace::root().lock_pinned().print_tree();
}
