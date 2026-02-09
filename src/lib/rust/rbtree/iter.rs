use core::{marker::PhantomData, ptr::NonNull};

use super::{RbNodeBase, RbTreeBase};

// 迭代器：泛型 IterType 决定遍历策略
pub struct RbNodeIter<'a, K: Ord + Sized, I, A> {
    pub root: NonNull<RbNodeBase<K, I, A>>,
    pub next: Option<NonNull<RbNodeBase<K, I, A>>>,
    pub _phantom: PhantomData<&'a mut RbNodeBase<K, I, A>>,
}

impl<'a, K: Ord + Sized, A> Iterator for RbNodeIter<'a, K, (), A> {
    type Item = &'a mut RbNodeBase<K, (), A>;

    fn next(&mut self) -> Option<Self::Item> {
        let mut node_ptr = self.next.take()?;
        // 计算后继节点
        let next = unsafe {
            // 如果有右子树，后继为右子树中最左的节点
            if let Some(mut r) = node_ptr.as_ref().right {
                while let Some(l) = r.as_ref().left {
                    r = l;
                }
                Some(r)
            } else {
                // 向上爬父节点，直到当前节点是父节点的左子节点
                let mut child = node_ptr;

                let mut result = None;
                while let Some(parent) = child.as_ref().get_parent() {
                    // 如果 child 是 parent 的左子节点，parent 就是后继
                    if let Some(left) = parent.as_ref().left {
                        if left == child {
                            result = Some(parent);
                            break;
                        }
                    }
                    child = parent;
                }
                result
            }
        };
        self.next = next;
        // 将 NonNull 转为可变引用返回（不安全）
        Some(unsafe { node_ptr.as_mut() })
    }
}

// 允许直接对 &mut RbNode 使用 for ... in &mut rb_node（默认中序）
impl<'a, K: Ord + Sized, A> IntoIterator for &'a mut RbNodeBase<K, (), A> {
    type Item = &'a mut RbNodeBase<K, (), A>;
    type IntoIter = RbNodeIter<'a, K, (), A>;

    fn into_iter(self) -> RbNodeIter<'a, K, (), A> {
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

impl<'a, K: Ord + Sized, TA, NA> RbTreeBase<K, (), TA, NA> {
    pub fn iter(&mut self) -> RbNodeIter<'_, K, (), NA> {
        let mut root = match self.root {
            Some(root) => root,
            None => {
                return RbNodeIter {
                    root: NonNull::dangling(),
                    next: None,
                    _phantom: PhantomData,
                };
            }
        };

        unsafe { root.as_mut().into_iter() }
    }
}
