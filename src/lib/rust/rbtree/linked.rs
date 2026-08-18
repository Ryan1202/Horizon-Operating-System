use core::{cmp, marker::PhantomData, mem::offset_of, ptr::NonNull};

use crate::lib::rust::{
    field::field_path,
    list::{ListIterator, ListMember},
    rbtree::augment::AugmentLinkHead,
};

use super::{
    super::list::{ListHead, ListNode},
    RbNodeBase, RbTreeBase,
    augment::{Augment, AugmentLink},
    iter::RbNodeIter,
};

#[macro_export]
macro_rules! linked_augment {
    ($node:expr) => {
        $node.augment.augment
    };
}

#[derive(Default, PartialEq, Debug)]
pub struct LinkedIter;

pub type LinkedRbNodeBase<K, A> = RbNodeBase<K, LinkedIter, Linked<K, A>>;
pub type LinkedRbNode<K> = LinkedRbNodeBase<K, ()>;
pub type LinkedRbTreeBase<K, A, NA> =
    RbTreeBase<K, LinkedIter, LinkedHead<K, A, NA>, Linked<K, NA>>;
pub type LinkedRbTree<K> = LinkedRbTreeBase<K, (), ()>;

pub type RbListMember<K, A> = field_path!(
    LinkedRbNodeBase<K, A> => augment,
    Linked<K, A> => list_node,
);

impl<K: Sized, A, NA> AugmentLink<K, LinkedIter, LinkedHead<K, A, NA>, Linked<K, NA>>
    for LinkedRbNodeBase<K, NA>
{
    fn link_ext(
        &mut self,
        _tree: &mut LinkedRbTreeBase<K, A, NA>,
        new_node: &mut LinkedRbNodeBase<K, NA>,
        order: cmp::Ordering,
    ) {
        let cur = self.augment.get_list();
        match order {
            cmp::Ordering::Less => {
                unsafe { new_node.augment.get_list().add_before(cur) };
            }
            cmp::Ordering::Greater => {
                unsafe { new_node.augment.get_list().add_after(cur) };
            }
            cmp::Ordering::Equal => {
                unreachable!("Duplicate keys are not allowed in RbTree");
            }
        }
    }
    fn unlink_ext(&mut self, tree: &mut LinkedRbTreeBase<K, A, NA>) {
        unsafe { tree.augment.list_head.delete(self) };
    }
}

impl<K: Sized, A, NA> AugmentLinkHead<K, LinkedIter, LinkedHead<K, A, NA>, Linked<K, NA>>
    for LinkedRbTreeBase<K, A, NA>
{
    fn init(&mut self, node: &mut RbNodeBase<K, LinkedIter, Linked<K, NA>>) {
        self.augment.list_head.add_head(node);
    }
}

impl<K> Augment for LinkedRbNodeBase<K, ()> {}

#[repr(C)]
#[derive(Debug)]
pub struct Linked<K, A> {
    list_node: ListNode<RbListMember<K, A>>,
    pub augment: A,
}

#[repr(C)]
#[derive(Default, Debug)]
pub struct LinkedHead<K, A, NA> {
    pub list_head: ListHead<RbListMember<K, NA>>,
    pub augment: A,
}

impl<K, A: Default> Default for Linked<K, A> {
    fn default() -> Self {
        Self {
            list_node: ListNode::new(),
            augment: Default::default(),
        }
    }
}

impl<K, A> Linked<K, A> {
    pub fn get_list(&mut self) -> &mut ListNode<RbListMember<K, A>> {
        &mut self.list_node
    }
}

impl<K, A, NA> LinkedHead<K, A, NA> {
    pub unsafe fn iter(&self) -> ListIterator<'_, RbListMember<K, NA>> {
        unsafe { self.list_head.iter() }
    }
}

impl<K, A, NA> LinkedRbTreeBase<K, A, NA> {
    pub const fn linked_offset() -> usize {
        offset_of!(Linked<K, NA>, list_node) + offset_of!(Self, augment)
    }

    pub fn _linked_init(&mut self, augment: A) {
        self._init();
        self.augment.augment = augment;
        unsafe { self.augment.list_head.init() };
    }
}

const impl<K, I: const Default, A> Default for LinkedRbTreeBase<K, I, A> {
    fn default() -> Self {
        Self::_new(LinkedHead {
            list_head: ListHead::default(),
            augment: I::default(),
        })
    }
}

impl<K, A> LinkedRbTreeBase<K, (), A> {
    pub fn init(&mut self) {
        self._linked_init(());
    }
}

impl<K: Sized> LinkedRbNode<K> {
    pub const fn new(key: K) -> Self {
        Self::linked_new(key, ())
    }
}

impl<K: Sized, A> LinkedRbNodeBase<K, A> {
    pub const fn linked_new(key: K, augment: A) -> Self {
        Self::_new(
            key,
            Linked {
                list_node: ListNode::new(),
                augment,
            },
        )
    }
}

impl<'a, K: Ord + Sized, A> Iterator for RbNodeIter<'a, K, LinkedIter, Linked<K, A>> {
    type Item = &'a mut LinkedRbNodeBase<K, A>;
    fn next(&mut self) -> Option<Self::Item> {
        let mut next_node = self.next?;

        let next = unsafe { next_node.as_mut() }
            .augment
            .list_node
            .next()
            .map(|next| unsafe { <RbListMember<K, A> as ListMember>::owner_of(next) });

        self.next = next;

        Some(unsafe { next_node.as_mut() })
    }
}

impl<'a, K, A> IntoIterator for &'a mut LinkedRbNodeBase<K, A>
where
    K: 'a + Ord + Sized,
    LinkedRbNodeBase<K, A>: Augment,
{
    type Item = &'a mut LinkedRbNodeBase<K, A>;
    type IntoIter = RbNodeIter<'a, K, LinkedIter, Linked<K, A>>;

    fn into_iter(self) -> Self::IntoIter {
        unsafe {
            let self_node = NonNull::from(self);
            let mut n = self_node;
            while let Some(left) = n.as_ref().left {
                n = left;
            }
            RbNodeIter {
                root: self_node,
                next: Some(n),
                _phantom: PhantomData,
            }
        }
    }
}

impl<'a, K: Ord + Sized, A, NA> LinkedRbTreeBase<K, A, NA> {
    pub fn iter(&mut self) -> RbNodeIter<'a, K, LinkedIter, Linked<K, NA>> {
        let root = match self.root {
            Some(root) => root,
            None => {
                return RbNodeIter {
                    root: NonNull::dangling(),
                    next: None,
                    _phantom: PhantomData,
                };
            }
        };

        let first_node = unsafe { self.augment.list_head.iter() }.next();
        let first_node = match first_node {
            Some(v) => v,
            None => {
                return RbNodeIter {
                    root,
                    next: None,
                    _phantom: PhantomData,
                };
            }
        };

        RbNodeIter {
            root,
            next: Some(first_node),
            _phantom: PhantomData,
        }
    }
}
