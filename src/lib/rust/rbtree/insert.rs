use core::ptr::NonNull;

use super::{
    RbColor, RbNodeBase, RbNodeWithColor, RbTreeBase,
    augment::{Augment, ChangeSide},
};

struct InsertFixupContext<'a, K: Sized, I, A> {
    cur_node: &'a mut RbNodeBase<K, I, A>,
    uncle: Option<&'a mut RbNodeBase<K, I, A>>,
    parent: &'a mut RbNodeBase<K, I, A>,
    grandparent: &'a mut RbNodeBase<K, I, A>,
    is_left: bool,
    parent_is_left: bool,
}

impl<'a, K: Sized, I, A> InsertFixupContext<'a, K, I, A> {
    fn new(current: &'a mut RbNodeBase<K, I, A>) -> Option<Self> {
        let mut parent = current.get_parent()?;
        let mut _parent: &'a mut RbNodeBase<K, I, A> = unsafe { parent.as_mut() };
        Self::new_with_parent(current, _parent)
    }

    fn new_with_parent(
        cur_node: &'a mut RbNodeBase<K, I, A>,
        parent: &'a mut RbNodeBase<K, I, A>,
    ) -> Option<Self> {
        if parent.parent.get_node_ptr().is_null() {
            // parent 是根节点，直接将其染黑
            parent.set_color(RbColor::Black);
            return None;
        }
        let is_left = if let Some(left) = parent.left {
            left == NonNull::from_mut(cur_node)
        } else {
            false
        };

        let grandparent;
        let parent_is_left;
        let uncle;
        unsafe {
            grandparent = parent.get_parent().unwrap().as_mut();
            (parent_is_left, uncle) = if grandparent.left == Some(NonNull::from_mut(parent)) {
                (true, grandparent.right.map(|mut v| v.as_mut()))
            } else {
                (false, grandparent.left.map(|mut v| v.as_mut()))
            };
        }

        Some(InsertFixupContext {
            cur_node,
            uncle,
            parent,
            grandparent,
            is_left,
            parent_is_left,
        })
    }
}

impl<K: Sized, I, A> RbNodeBase<K, I, A> {
    pub(super) fn insert_fixup<TA>(&mut self, tree: &mut RbTreeBase<K, I, TA, A>)
    where
        RbNodeBase<K, I, A>: Augment,
    {
        if let RbColor::Black = self.parent.get_color() {
            // 当前节点为黑色，无需修复
            return;
        }

        let mut context = match InsertFixupContext::new(self) {
            Some(ctx) => ctx,
            None => return,
        };

        while let RbColor::Red = context.parent.get_color() {
            if let Some(uncle) = context.uncle
                && let RbColor::Red = uncle.get_color()
            {
                // 情况1：叔节点为红色
                context.parent.set_color(RbColor::Black);
                uncle.set_color(RbColor::Black);
                context.grandparent.set_color(RbColor::Red);

                context = match InsertFixupContext::new(context.grandparent) {
                    Some(ctx) => ctx,
                    None => {
                        break;
                    }
                };
                continue;
            }

            let _grandparent = context.grandparent;
            let ggp = NonNull::new(_grandparent.parent.get_node_ptr());

            let (new_grandparent, side) = match (context.is_left, context.parent_is_left) {
                (true, true) => {
                    // 右旋 grandparent
                    let new = _grandparent.right_rotate(context.parent);
                    _grandparent.recalc(ChangeSide::Left);
                    (new, ChangeSide::Right)
                }
                (false, false) => {
                    // 左旋 grandparent
                    let new = _grandparent.left_rotate(context.parent);
                    _grandparent.recalc(ChangeSide::Right);
                    (new, ChangeSide::Left)
                }
                (true, false) => {
                    // 右旋 parent
                    // 左旋 grandparent
                    let parent = context.parent;
                    let cur_node = context.cur_node;

                    context.parent = parent.right_rotate(cur_node);
                    let new = _grandparent.left_rotate(context.parent);

                    parent.recalc(ChangeSide::Left);
                    _grandparent.recalc(ChangeSide::Right);
                    (new, ChangeSide::Both)
                }
                (false, true) => {
                    // 左旋 parent
                    // 右旋 grandparent
                    let parent = context.parent;
                    let cur_node = context.cur_node;

                    context.parent = parent.left_rotate(&mut *cur_node);
                    let new = _grandparent.right_rotate(context.parent);

                    parent.recalc(ChangeSide::Right);
                    _grandparent.recalc(ChangeSide::Left);
                    (new, ChangeSide::Both)
                }
            };

            new_grandparent.recalc(side);
            if let Some(mut ggp) = ggp {
                let ggp_ref = unsafe { ggp.as_mut() };
                let ggp_is_left = ggp_ref
                    .left
                    .is_some_and(|left| left == NonNull::from(_grandparent));

                ggp_ref.set_child(ggp_is_left, Some(NonNull::from_mut(new_grandparent)));
                break;
            } else {
                // grandparent 是根节点，更新树的根节点指针
                new_grandparent.parent = RbNodeWithColor::null_node();
                tree.root = Some(NonNull::from_mut(new_grandparent));
                break;
            }
        }
        if let Some(mut root) = tree.root {
            unsafe { root.as_mut() }.set_color(RbColor::Black);
        }
        #[cfg(debug_assertions)]
        {
            super::relation_assert(unsafe { tree.root.unwrap().as_ref() });
        }
    }
}
