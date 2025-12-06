use core::{cmp, ptr::NonNull};

use super::RbNodeBase;

pub enum ChangeSide {
    Left,
    Right,
    Both,
}

#[allow(unused_variables)]
pub trait Augment {
    /// 在节点的子树发生变化后，重新计算该节点的增强信息
    fn recalc(&mut self, side: ChangeSide) {}
    /// 在重新平衡后，将增强信息从节点传播到子树的根节点
    fn propagate(&mut self, root: NonNull<Self>) {}
}

#[allow(unused_variables)]
pub trait AugmentLink<K: Sized, I, NA> {
    fn link_ext(&mut self, new_node: &mut RbNodeBase<K, I, NA>, order: cmp::Ordering) {}
    fn unlink_ext(&mut self) {}
}
