use core::{mem, ptr::NonNull};

use super::{
    RbColor, RbNodeBase, RbNodeWithColor, RbTreeBase,
    augment::{Augment, AugmentLink, ChangeSide},
    iter::RbNodeIter,
};

fn replace_child<K: Ord + Sized, I, TA, NA>(
    parent: Option<NonNull<RbNodeBase<K, I, NA>>>,
    old_child: NonNull<RbNodeBase<K, I, NA>>,
    child: &mut RbNodeBase<K, I, NA>,
    tree: &mut RbTreeBase<K, I, TA, NA>,
) {
    let color = child.get_color();
    if let Some(mut parent) = parent {
        let parent = unsafe { parent.as_mut() };

        // 验证 parent 确实包含 old_child（调试时捕获不一致）
        debug_assert!(
            parent.left == Some(old_child) || parent.right == Some(old_child),
            "replace_child: parent does not contain old_child"
        );

        child.parent = RbNodeWithColor::new(NonNull::from_mut(parent), color);
        if let Some(left) = parent.left
            && left == old_child
        {
            parent.left = Some(NonNull::from_mut(child));
        } else {
            parent.right = Some(NonNull::from_mut(child));
        };
    } else {
        child.parent = RbNodeWithColor::null_node();
        tree.root = Some(NonNull::from_mut(child));
    }
}

impl<'a, K: Ord + Sized, I, A> RbNodeBase<K, I, A>
where
    &'a mut RbNodeBase<K, I, A>: IntoIterator<Item = &'a mut RbNodeBase<K, I, A>>,
    RbNodeIter<'a, K, I, A>: Iterator<Item = &'a mut RbNodeBase<K, I, A>>,
    K: 'a,
    I: 'a,
    A: 'a,
{
    /// 删除节点与父节点都是黑节点时调用，进行重新平衡
    fn rebalance_black_height<TA>(
        &mut self,
        tree: &mut RbTreeBase<K, I, TA, A>,
        parent: &mut RbNodeBase<K, I, A>,
        is_left: bool,
    ) where
        RbNodeBase<K, I, A>: Augment,
    {
        let mut parent = parent;
        let mut is_left = is_left;

        let f = |mut v: NonNull<RbNodeBase<K, I, A>>| {
            let tmp = unsafe { v.as_mut() };
            let color = tmp.get_color();
            (tmp, color)
        };

        loop {
            let (mut grandparent, mut parent_color) = parent.get_parent_and_color();

            if is_left {
                debug_assert!(
                    parent.right.is_some(),
                    "RbNode is left child but has no right sibling!"
                );
                let mut sibling = unsafe { parent.right.expect("no right").as_mut() };

                if let RbColor::Red = sibling.get_color() {
                    // 兄弟节点为红色，进行旋转
                    let sibling_left = sibling.left;
                    // sibling_left 必须存在（红色 sibling 的一个子不应该为空）
                    debug_assert!(
                        sibling_left.is_some(),
                        "RbNode left child of a red sibling node is null!"
                    );
                    parent.left_rotate(sibling);

                    let sibling_left = unsafe { sibling_left.expect("no left").as_mut() };

                    parent.recalc(ChangeSide::Right);
                    sibling.recalc(ChangeSide::Both);
                    replace_child(grandparent, NonNull::from_mut(parent), sibling, tree);

                    grandparent = Some(NonNull::from_mut(sibling));
                    parent_color = RbColor::Red;
                    sibling = sibling_left;
                }

                let left = sibling.left.map(f);
                let right = sibling.right.map(f);

                let new_parent = match (left, right) {
                    (None | Some((_, RbColor::Black)), None | Some((_, RbColor::Black))) => {
                        sibling.set_color(RbColor::Red);

                        if let RbColor::Red = parent_color {
                            parent.set_color(RbColor::Black);
                        } else if let Some(mut grandparent) = grandparent {
                            let grandparent = unsafe { grandparent.as_mut() };

                            is_left = grandparent
                                .left
                                .is_some_and(|left| left == NonNull::from_mut(parent));
                            parent = grandparent;

                            continue;
                        }
                        // 达成平衡，退出
                        break;
                    }
                    (_, Some((far, RbColor::Red))) => {
                        let close = sibling.left;

                        parent.left_rotate(sibling);

                        far.set_color(RbColor::Black);

                        if let Some(mut close) = close {
                            unsafe { close.as_mut() }.set_parent(NonNull::from_mut(parent));
                        }

                        parent.recalc(ChangeSide::Right);
                        sibling.recalc(ChangeSide::Left);

                        sibling
                    }
                    (Some((close, RbColor::Red)), None | Some((_, RbColor::Black))) => {
                        let close_right = close.right;
                        let close_left = close.left;

                        sibling.right_rotate(close);
                        parent.left_rotate(close);

                        sibling.set_color(RbColor::Black);

                        if let Some(mut close_left) = close_left {
                            unsafe { close_left.as_mut() }.set_parent(NonNull::from_mut(parent));
                        }
                        if let Some(mut close_right) = close_right {
                            unsafe { close_right.as_mut() }.set_parent(NonNull::from_mut(sibling));
                        }

                        parent.recalc(ChangeSide::Right);
                        sibling.recalc(ChangeSide::Left);
                        close.recalc(ChangeSide::Both);
                        close
                    }
                };

                replace_child(grandparent, NonNull::from_mut(parent), new_parent, tree);

                break;
            } else {
                debug_assert!(
                    parent.left.is_some(),
                    "RbNode is right child but has no left sibling!"
                );
                let mut sibling = unsafe { parent.left.expect("no left").as_mut() };

                if let RbColor::Red = sibling.get_color() {
                    // 兄弟节点为红色，进行旋转
                    let sibling_right = sibling.right;
                    debug_assert!(
                        sibling_right.is_some(),
                        "RbNode right child of a red sibling node is null!"
                    );
                    parent.right_rotate(sibling);

                    let sibling_right = unsafe { sibling_right.expect("no right").as_mut() };

                    parent.recalc(ChangeSide::Left);
                    sibling.recalc(ChangeSide::Both);

                    replace_child(grandparent, NonNull::from_mut(parent), sibling, tree);
                    grandparent = Some(NonNull::from_mut(sibling));
                    parent_color = RbColor::Red;
                    sibling = sibling_right;
                }

                let left = sibling.left.map(f);
                let right = sibling.right.map(f);

                let new_parent = match (left, right) {
                    (None | Some((_, RbColor::Black)), None | Some((_, RbColor::Black))) => {
                        sibling.set_color(RbColor::Red);

                        if let RbColor::Red = parent_color {
                            parent.set_color(RbColor::Black);
                        } else if let Some(mut grandparent) = grandparent {
                            let grandparent = unsafe { grandparent.as_mut() };

                            is_left = grandparent
                                .left
                                .is_some_and(|left| left == NonNull::from_mut(parent));
                            parent = grandparent;
                            continue;
                        }
                        // 达成平衡，退出
                        break;
                    }
                    (Some((far, RbColor::Red)), _) => {
                        let close = sibling.right;

                        parent.right_rotate(sibling);

                        far.set_color(RbColor::Black);

                        if let Some(mut close) = close {
                            unsafe {
                                close.as_mut().set_parent(NonNull::from_mut(parent));
                            }
                        }

                        parent.recalc(ChangeSide::Left);
                        sibling.recalc(ChangeSide::Right);

                        sibling
                    }
                    (_, Some((close, RbColor::Red))) => {
                        let close_left = close.left;
                        let close_right = close.right;

                        sibling.left_rotate(close);
                        parent.right_rotate(close);

                        sibling.set_color(RbColor::Black);

                        if let Some(mut close_left) = close_left {
                            unsafe { close_left.as_mut() }.set_parent(NonNull::from_mut(sibling));
                        }
                        if let Some(mut close_right) = close_right {
                            unsafe { close_right.as_mut() }.set_parent(NonNull::from_mut(parent));
                        }

                        parent.recalc(ChangeSide::Left);
                        sibling.recalc(ChangeSide::Right);
                        close.recalc(ChangeSide::Both);

                        close
                    }
                };

                replace_child(grandparent, NonNull::from_mut(parent), new_parent, tree);

                break;
            }
        }
    }

    pub fn delete<TA>(&mut self, tree: &mut RbTreeBase<K, I, TA, A>)
    where
        RbNodeBase<K, I, A>: Augment + AugmentLink<K, I, A>,
    {
        let (parent, color) = self.get_parent_and_color();
        let (left, right) = (self.left, self.right);

        // 先把 parent 字段替换为空表示我们将把 parent 信息暂存到 parent_copy
        let parent_copy = mem::take(&mut self.parent);

        let old = NonNull::from_mut(self);
        let (new, balance) = match (left, right) {
            (Some(mut _left), Some(mut right)) => {
                // 有两个子节点，找到后继节点进行替换删除
                let successor = unsafe { right.as_mut() }
                    .into_iter()
                    .next()
                    .expect("right RbNode exist but failed to find successor!");

                let ptr = NonNull::from_mut(successor);
                let successor_child = successor.right;
                let successor_color = successor.get_color();

                let (successor_parent, is_left) = if ptr == right {
                    // 后继节点就是右子节点本身，直接提升右子节点
                    successor.parent = parent_copy;
                    successor.left = left;

                    unsafe { _left.as_mut() }.set_parent(ptr);

                    successor.recalc(ChangeSide::Left);

                    (successor, false)
                } else {
                    let original;
                    unsafe {
                        self.swap(successor);

                        original = successor;
                        original.parent = parent_copy;

                        if let Some(mut left) = original.left {
                            left.as_mut().set_parent(NonNull::from_ref(original));
                        }

                        original
                            .right
                            .unwrap()
                            .as_mut()
                            .set_parent(NonNull::from_mut(original));
                    };

                    let successor_parent = unsafe {
                        self.get_parent()
                            .expect("Failed to get parent RbNode of successor")
                            .as_mut()
                    };
                    successor_parent.left = self.right;

                    successor_parent.propagate(NonNull::from(original));

                    (successor_parent, true)
                };

                let mut balance = None;

                if let Some(mut right) = successor_child {
                    // 后继节点只有右子节点，
                    // 当后继节点为黑色时，右子节点必须是红色
                    // 当后继节点为红色时，右子节点必须是黑色
                    // 所以将右子节点染黑即可维持黑高不变

                    let _right = unsafe { right.as_mut() };

                    _right.parent =
                        RbNodeWithColor::new(NonNull::from_mut(successor_parent), RbColor::Black);
                } else if let RbColor::Black = successor_color {
                    // 否则黑高减一，重新平衡
                    balance = Some((successor_parent, is_left));
                }

                (Some(ptr), balance)
            }
            (Some(mut left), None) => {
                // 只有左子节点，使用左子节点替换自己
                let _left = unsafe { left.as_mut() };
                _left.parent = parent_copy;

                // 两个节点中必有一个为黑色，替换后将左子节点涂黑以维持黑高不变
                _left.set_color(RbColor::Black);

                (Some(left), None)
            }
            (None, Some(mut right)) => {
                // 只有右子节点，使用右子节点替换自己
                let _right = unsafe { right.as_mut() };
                _right.parent = parent_copy;

                // 两个节点中必有一个为黑色，替换后将右子节点涂黑以维持黑高不变
                _right.set_color(RbColor::Black);

                (Some(right), None)
            }
            (None, None) => {
                // 无子节点，直接删除
                if let Some(mut parent) = parent
                    && let RbColor::Black = color
                {
                    let _parent = unsafe { parent.as_mut() };
                    let was_left = _parent.left.is_some_and(|left| left == old);

                    (None, Some((_parent, was_left)))
                } else {
                    (None, None)
                }
            }
        };

        match parent {
            Some(mut parent) => {
                let parent = unsafe { parent.as_mut() };
                // 调试时断言 parent 确实包含 old，避免把 new 接到错误侧
                debug_assert!(
                    parent.left == Some(old) || parent.right == Some(old),
                    "parent does not contain the old child during delete"
                );

                if let Some(left) = parent.left
                    && left == old
                {
                    parent.left = new;
                } else {
                    parent.right = new;
                }

                parent.propagate(tree.root.unwrap());
            }
            None => {
                // 删除的是根节点，更新树的根节点指针
                tree.root = new;
            }
        }

        if let Some((parent, is_left)) = balance {
            self.rebalance_black_height(tree, parent, is_left);
        }

        #[cfg(debug_assertions)]
        if let Some(root) = tree.root {
            super::relation_assert(unsafe { root.as_ref() });
        }

        // 清理当前节点
        self.unlink_ext();
        self.update(None, None);
    }
}
