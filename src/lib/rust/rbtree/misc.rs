use core::{mem::replace, ptr::NonNull};

use super::{RbNodeBase, RbNodeWithColor};

impl<K: Sized, I, A> RbNodeBase<K, I, A> {
    #[inline(always)]
    pub(super) fn set_child(&mut self, is_left: bool, child: Option<NonNull<RbNodeBase<K, I, A>>>) {
        if is_left {
            self.left = child;
        } else {
            self.right = child;
        }
    }

    /// 使用`left`替换`self`的原左子树然后右旋，返回新的子树根节点引用
    ///
    /// 该方法只负责节点间的连接关系调整，不处理父节点的连接
    #[inline(always)]
    pub(super) fn right_rotate<'a>(
        &mut self,
        left: &'a mut RbNodeBase<K, I, A>,
    ) -> &'a mut RbNodeBase<K, I, A> {
        // 右旋逻辑实现
        let left_right = left.right;

        left.update(left.left, Some(NonNull::from_mut(self)));

        let parent = replace(
            &mut self.parent,
            RbNodeWithColor::new(NonNull::from_mut(left), left.get_color()),
        );
        left.parent = parent;

        self.update(left_right, self.right);
        if let Some(mut lr) = left_right {
            unsafe {
                lr.as_mut().set_parent(NonNull::from_mut(self));
            }
        }
        left
    }

    /// 使用`right`替换`self`的原右子树然后左旋，返回新的子树根节点引用
    ///
    /// 该方法只负责节点间的连接关系调整，不处理父节点的连接
    #[inline(always)]
    pub(super) fn left_rotate<'a>(
        &mut self,
        right: &'a mut RbNodeBase<K, I, A>,
    ) -> &'a mut RbNodeBase<K, I, A> {
        // 左旋逻辑实现
        let right_left = right.left;

        right.update(Some(NonNull::from_mut(self)), right.right);

        let parent = replace(
            &mut self.parent,
            RbNodeWithColor::new(NonNull::from_mut(right), right.get_color()),
        );
        right.parent = parent;

        self.update(self.left, right_left);
        if let Some(mut rl) = right_left {
            unsafe {
                rl.as_mut().set_parent(NonNull::from_mut(self));
            }
        }
        right
    }
}
