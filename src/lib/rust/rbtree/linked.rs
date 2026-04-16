use core::{cmp, marker::PhantomData, mem::offset_of, pin::Pin, ptr::NonNull};

use crate::{
    container_of,
    lib::rust::{list::ListIterator, rbtree::augment::AugmentLinkHead},
    list_owner,
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
                new_node.augment.get_list().add_before(cur);
            }
            cmp::Ordering::Greater => {
                new_node.augment.get_list().add_after(cur);
            }
            cmp::Ordering::Equal => {
                unreachable!("Duplicate keys are not allowed in RbTree");
            }
        }
    }
    fn unlink_ext(&mut self, tree: &mut LinkedRbTreeBase<K, A, NA>) {
        let list_node = self.augment.get_list();
        unsafe { Pin::new_unchecked(&mut tree.augment.list_head) }.del(list_node);
    }
}

impl<K: Sized, A, NA> AugmentLinkHead<K, LinkedIter, LinkedHead<K, A, NA>, Linked<K, NA>>
    for LinkedRbTreeBase<K, A, NA>
{
    fn init(&mut self, node: &mut RbNodeBase<K, LinkedIter, Linked<K, NA>>) {
        unsafe { Pin::new_unchecked(&mut self.augment.list_head) }
            .add_head(node.augment.get_list());
    }
}

impl<K> Augment for LinkedRbNodeBase<K, ()> {}

#[repr(C)]
#[derive(PartialEq, Debug)]
pub struct Linked<K, A> {
    list_node: ListNode<LinkedRbNodeBase<K, A>>,
    pub augment: A,
}

#[repr(C)]
#[derive(Default, Debug)]
pub struct LinkedHead<K, A, NA> {
    pub list_head: ListHead<LinkedRbNodeBase<K, NA>>,
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
    pub fn get_list(&mut self) -> Pin<&mut ListNode<LinkedRbNodeBase<K, A>>> {
        unsafe { Pin::new_unchecked(&mut self.list_node) }
    }
}

impl<K, A, NA> LinkedHead<K, A, NA> {
    pub fn iter(&mut self) -> ListIterator<LinkedRbNodeBase<K, NA>> {
        unsafe { Pin::new_unchecked(&mut self.list_head) }
            .iter(LinkedRbTreeBase::<K, A, NA>::linked_offset())
    }
}

impl<K, A, NA> LinkedRbTreeBase<K, A, NA> {
    pub const fn linked_offset() -> usize {
        offset_of!(Linked<K, NA>, list_node) + offset_of!(Self, augment)
    }

    pub fn _linked_init(&mut self, augment: A) {
        self._init();
        self.augment.augment = augment;
        unsafe { Pin::new_unchecked(&mut self.augment.list_head) }.init();
    }

    pub const fn _empty(augment: A) -> Self {
        Self::_new(LinkedHead {
            list_head: ListHead::empty(),
            augment,
        })
    }
}

impl<K, A> LinkedRbTreeBase<K, (), A> {
    pub fn init(&mut self) {
        self._linked_init(());
    }

    pub const fn empty() -> Self {
        Self::_empty(())
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
        let link = unsafe { next_node.as_mut() }
            .augment
            .list_node
            .link
            .as_ref();

        let next = link
            .map(|current| current.next)
            .map(|next| container_of!(next, ListNode<LinkedRbNodeBase<K, A>>, link))
            .map(|next| list_owner!(next, Linked<K, A>, list_node))
            .map(|next| container_of!(next, LinkedRbNodeBase<K, A>, augment));

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

        let list_head = unsafe { Pin::new_unchecked(&mut self.augment.list_head) };
        let first_node = list_head
            .iter(offset_of!(LinkedHead<K, A, NA>, list_head))
            .next();
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
