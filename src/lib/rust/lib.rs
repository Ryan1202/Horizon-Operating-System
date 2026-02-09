/// 仅用于本机测试
mod list;
mod rbtree;
mod spinlock;

#[macro_export]
macro_rules! container_of {
    ($ptr:expr, $container:ty, $field:ident) => {{
        use core::mem::offset_of;

        let _ptr: NonNull<_> = $ptr;
        let offset = offset_of!($container, $field);
        unsafe { _ptr.byte_offset(-(offset as isize)).cast::<$container>() }
    }};
}

#[macro_export]
macro_rules! container_of_enum {
    ($ptr:expr, $container:ty, $field:expr) => {{
        use core::mem::offset_of;

        let _ptr: NonNull<_> = $ptr;
        let offset = offset_of!($container, $field);
        unsafe { _ptr.byte_offset(-(offset as isize)).cast::<$container>() }
    }};
}

#[cfg(test)]
mod tests {
    use std::ptr::NonNull;

    use rand::seq::SliceRandom;
    use rbtree_ext::RBTree;

    use crate::rbtree::{RbNodeWithColor, linked::LinkedRbTree};

    #[test]
    fn test1() {
        extern crate std;
        use std::boxed::Box;

        use super::rbtree::{RbNodeBase, validation};

        let mut tree: LinkedRbTree<usize, (), ()> = LinkedRbTree::default();
        tree.init(());
        let mut _tree = RBTree::new();
        let mut rng = rand::rng();
        let mut keys: Vec<usize> = (0..15).collect();
        keys.shuffle(&mut rng);
        // let keys = vec![1, 9, 2, 8, 3, 7, 4, 6, 5];
        // let keys = vec![3, 12, 2, 9, 1, 6, 0, 10, 11, 4, 7, 5, 14, 8, 13];
        // let keys = vec![2, 0, 5, 3, 8, 10, 4, 1, 12, 13, 9, 6, 14, 11, 7];
        // let keys = vec![3, 1, 13, 2, 5, 14, 11, 0, 9, 4, 7, 8, 6, 12, 10];
        // let keys = vec![13, 10, 1, 7, 3, 11, 9, 4, 6, 12, 8, 0, 2, 5, 14];
        println!("keys: {:?}", keys);
        for key in keys.clone() {
            let node = Box::new(RbNodeBase::new(key));
            tree.insert(Box::leak(node).into());
            _tree.insert(key, key);

            let root = unsafe { tree.root.unwrap().as_ref() };
            assert_eq!(
                validation(NonNull::from_ref(root), root.left, 1),
                validation(NonNull::from_ref(root), root.right, 1)
            );
            assert_eq!(root.parent, RbNodeWithColor::null_node());
        }
        for key in keys {
            tree.delete(&key);
            _tree.remove(&key);

            if let Some(mut root) = tree.root {
                let mut _root = unsafe { root.as_mut() };
                assert_eq!(
                    validation(root, _root.left, 1),
                    validation(root, _root.right, 1)
                );

                // let mut iter = _root.inorder_iter();
                let mut _iter = _tree.iter();
                // while let Some(node_ptr) = iter.next() {
                //     let node = unsafe { node_ptr.as_ref() };
                //     print!("{} ", node.key);
                // }
                for (a, b) in tree.iter().zip(_tree.iter()) {
                    assert_eq!(&a.key, b.0);
                }
            }
        }
    }
}
