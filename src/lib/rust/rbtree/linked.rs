use core::{cmp, marker::PhantomData, pin::Pin, ptr::NonNull};

use crate::{container_of, list_first_owner, list_owner};

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

impl<K: Sized, NA> AugmentLink<K, LinkedIter, Linked<K, NA>> for LinkedRbNodeBase<K, NA> {
    fn link_ext(&mut self, new_node: &mut LinkedRbNodeBase<K, NA>, order: cmp::Ordering) {
        let mut list_node = NonNull::from(&new_node.augment.list_node);
        let cur = unsafe { Pin::new_unchecked(list_node.as_mut()) };
        let node = unsafe { Pin::new_unchecked(&mut self.augment.list_node) };
        match order {
            cmp::Ordering::Less => {
                cur.add_before(node);
            }
            cmp::Ordering::Greater => {
                cur.add_after(node);
            }
            cmp::Ordering::Equal => {
                unreachable!("Duplicate keys are not allowed in RbTree");
            }
        }
    }
    fn unlink_ext(&mut self) {
        let list_node = unsafe { Pin::new_unchecked(&mut self.augment.list_node) };
        list_node.del();
    }
}

impl<K: Sized, A, NA> AugmentLink<K, LinkedIter, Linked<K, NA>> for LinkedRbTreeBase<K, A, NA> {
    fn link_ext(&mut self, new_node: &mut LinkedRbNodeBase<K, NA>, _order: cmp::Ordering) {
        let mut head = unsafe { Pin::new_unchecked(&mut self.augment.list_head) };
        let list_node = unsafe { Pin::new_unchecked(&mut new_node.augment.list_node) };
        head.add_tail(list_node);
    }
    fn unlink_ext(&mut self) {}
}

impl<K> Augment for LinkedRbNodeBase<K, ()> {}

#[derive(PartialEq, Debug)]
pub struct Linked<K, A> {
    pub list_node: ListNode<LinkedRbNodeBase<K, A>>,
    pub augment: A,
}

#[derive(Default, PartialEq, Debug)]
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

impl<K, A, NA> LinkedRbTreeBase<K, A, NA> {
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
        let list_node = unsafe { next_node.as_mut() }.augment.list_node.next?;

        let next = list_owner!(list_node, Linked<K, A>, list_node);
        let next = container_of!(next, LinkedRbNodeBase<K, A>, augment);
        self.next = Some(next);

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

        let list_head = &self.augment.list_head;
        let first_node = match list_first_owner!(LinkedHead<K, A, NA>, list_head, list_head) {
            Some(first_node) => {
                container_of!(first_node, LinkedRbNodeBase<K, NA>, augment)
            }
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
