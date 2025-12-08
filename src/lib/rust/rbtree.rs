use core::{cmp, marker::PhantomData, ptr::NonNull};

use {
    augment::{Augment, AugmentLink},
    iter::RbNodeIter,
};

pub mod augment;
mod delete;
mod insert;
pub mod iter;
pub mod linked;
mod misc;

#[derive(PartialEq, Eq, Debug)]
#[repr(transparent)]
pub struct RbNodeWithColor<K: Sized, I, A>(*mut RbNodeBase<K, I, A>);

#[derive(Clone, Copy)]
pub enum RbColor {
    Red,
    Black,
}

/// 红黑树节点
/// 对齐4字节以利用最低位存储颜色信息
/// parent指针的最低位用于存储颜色信息：0表示黑色，1表示红色
/// 默认右偏
#[derive(Default, PartialEq, Debug)]
#[repr(align(4))]
pub struct RbNodeBase<K: Sized, I, A> {
    pub(super) parent: RbNodeWithColor<K, I, A>,
    pub left: Option<NonNull<RbNodeBase<K, I, A>>>,
    pub right: Option<NonNull<RbNodeBase<K, I, A>>>,
    pub(super) key: K,
    pub augment: A,
    _phantom: PhantomData<I>,
}

impl<K: Sized, I> Augment for RbNodeBase<K, I, ()> {}
impl<K: Sized, A> AugmentLink<K, (), A> for RbNodeBase<K, (), A> {}
impl<K: Sized, A, NA> AugmentLink<K, (), NA> for RbTreeBase<K, (), A, NA> {}

/// 红黑树结构体
/// 对齐4字节以确保节点指针的最低位可用作颜色标志
/// 默认右偏
#[repr(align(4))]
pub struct RbTreeBase<K: Sized, I, TA, NA> {
    pub root: Option<NonNull<RbNodeBase<K, I, NA>>>,
    pub augment: TA,
}

pub trait RbSearch<K: Sized, Node, Compare: Fn(&K, &K) -> cmp::Ordering> {
    fn search_exact(&self, key: &K, compare: Compare) -> Option<NonNull<Node>> {
        self.search_closest(key, compare)
            .and_then(|(v, ord)| match ord {
                cmp::Ordering::Equal => Some(v),
                _ => None,
            })
    }

    fn search_closest(&self, key: &K, compare: Compare) -> Option<(NonNull<Node>, cmp::Ordering)>;
}

pub type RbTree<K> = RbTreeBase<K, (), (), ()>;
pub type AugmentedRbTree<K, TA, NA> = RbTreeBase<K, (), TA, NA>;
pub type RbNode<K> = RbNodeBase<K, (), ()>;
pub type AugmentedRbNode<K, A> = RbNodeBase<K, (), A>;

impl<K: Sized, I, A> RbNodeWithColor<K, I, A> {
    pub const fn null_node() -> Self {
        RbNodeWithColor(core::ptr::null_mut())
    }

    #[inline(always)]
    pub fn new(node: NonNull<RbNodeBase<K, I, A>>, color: RbColor) -> Self {
        match color {
            RbColor::Black => RbNodeWithColor(node.as_ptr()),
            RbColor::Red => RbNodeWithColor(node.as_ptr().map_addr(|addr| addr | 0x01)),
        }
    }

    #[inline(always)]
    pub fn get_node_ptr(&self) -> *mut RbNodeBase<K, I, A> {
        self.0.map_addr(|v| v & !0x01)
    }

    #[inline(always)]
    pub fn set_parent(&mut self, parent: NonNull<RbNodeBase<K, I, A>>) {
        self.0 = self
            .0
            .map_addr(|addr| (addr & 0x01) | (parent.as_ptr().addr() & !0x01));
    }

    #[inline(always)]
    pub fn set_color(&mut self, color: RbColor) {
        *self = match color {
            RbColor::Black => RbNodeWithColor(self.0.map_addr(|addr| addr & !0x01)),
            RbColor::Red => RbNodeWithColor(self.0.map_addr(|addr| addr | 0x01)),
        };
    }

    #[inline(always)]
    pub fn get_color(&self) -> RbColor {
        if self.0.addr() & 0x01 != 0 {
            RbColor::Red
        } else {
            RbColor::Black
        }
    }
}

impl<K: Sized, I, A> Default for RbNodeWithColor<K, I, A> {
    fn default() -> Self {
        RbNodeWithColor::null_node()
    }
}

impl<K: Sized> RbTreeBase<K, (), (), ()> {
    pub const fn new() -> Self {
        Self::_new(())
    }

    pub fn init(&mut self) {
        self._init();
    }
}

impl<K: Sized, I, TA, NA> RbTreeBase<K, I, TA, NA> {
    const fn _new(augment: TA) -> Self {
        RbTreeBase {
            root: None,
            augment,
        }
    }

    fn _init(&mut self) {
        self.root = None;
    }

    pub fn insert(&mut self, new_node: &mut RbNodeBase<K, I, NA>)
    where
        K: Ord + Sized,
        RbNodeBase<K, I, NA>: Augment + AugmentLink<K, I, NA>,
        RbTreeBase<K, I, TA, NA>: AugmentLink<K, I, NA>,
    {
        debug_assert!(
            new_node.get_parent().is_none() && new_node.left.is_none() && new_node.right.is_none(),
            "Inserting a node that is already in a tree!"
        );

        let key = &new_node.key;
        let parent = self
            .root
            .and_then(|root| unsafe { root.as_ref() }.search_closest_and_recalc(key, K::cmp));

        match parent {
            Some((mut parent_ptr, ordering)) => {
                let parent_ref = unsafe { parent_ptr.as_mut() };

                parent_ref.link_node(new_node, ordering);
                new_node.insert_fixup(self);

                parent_ref.link_ext(new_node, ordering);

                new_node.propagate(self.root.unwrap());
            }
            None => {
                // 树为空，插入为根节点
                new_node.parent = RbNodeWithColor::null_node();
                self.root = Some(NonNull::from_mut(new_node));

                self.link_ext(new_node, cmp::Ordering::Equal);
            }
        }
    }

    pub fn delete<'a>(&mut self, key: &K) -> Option<NonNull<RbNodeBase<K, I, NA>>>
    where
        &'a mut RbNodeBase<K, I, NA>: IntoIterator<Item = &'a mut RbNodeBase<K, I, NA>>,
        RbNodeBase<K, I, NA>: Augment + AugmentLink<K, I, NA>,
        RbNodeIter<'a, K, I, NA>: Iterator<Item = &'a mut RbNodeBase<K, I, NA>>,
        K: 'a + Ord + Sized,
        I: 'a,
        NA: 'a,
    {
        let target = self.search_exact(key, K::cmp)?;
        self.delete_node(target);
        Some(target)
    }

    pub fn delete_node<'a>(&mut self, mut target: NonNull<RbNodeBase<K, I, NA>>)
    where
        &'a mut RbNodeBase<K, I, NA>: IntoIterator<Item = &'a mut RbNodeBase<K, I, NA>>,
        RbNodeBase<K, I, NA>: Augment + AugmentLink<K, I, NA>,
        RbNodeIter<'a, K, I, NA>: Iterator<Item = &'a mut RbNodeBase<K, I, NA>>,
        K: 'a + Ord + Sized,
        I: 'a,
        NA: 'a,
    {
        let target_ref = unsafe { target.as_mut() };
        target_ref.delete(self);
    }
}

impl<K: Sized, I, NA> RbNodeBase<K, I, NA> {
    #[inline(always)]
    fn get_color(&self) -> RbColor {
        self.parent.get_color()
    }

    #[inline(always)]
    fn set_color(&mut self, color: RbColor) {
        self.parent.set_color(color);
    }

    #[inline(always)]
    fn get_parent_and_color(&self) -> (Option<NonNull<RbNodeBase<K, I, NA>>>, RbColor) {
        let color = self.parent.get_color();
        (NonNull::new(self.parent.get_node_ptr()), color)
    }

    #[inline(always)]
    pub fn get_parent(&self) -> Option<NonNull<RbNodeBase<K, I, NA>>> {
        NonNull::new(self.parent.get_node_ptr())
    }

    #[inline(always)]
    fn set_parent(&mut self, parent: NonNull<RbNodeBase<K, I, NA>>) {
        self.parent.set_parent(parent);
    }

    #[inline(always)]
    fn update(
        &mut self,
        left: Option<NonNull<RbNodeBase<K, I, NA>>>,
        right: Option<NonNull<RbNodeBase<K, I, NA>>>,
    ) {
        self.left = left;
        self.right = right;
    }

    pub const fn get_key(&self) -> &K {
        &self.key
    }

    /// 获取可变引用的Key
    ///
    /// # Safety
    ///
    /// 修改Key可能破坏红黑树的有序性，调用者必须确保修改后的Key仍然满足树的有序性要求，
    /// 或者当前节点不在树中
    pub unsafe fn get_key_mut(&mut self) -> &mut K {
        &mut self.key
    }

    /// 交换当前节点与另一个节点的位置
    ///
    /// 只交换数据，不修改父节点的指针和Key
    unsafe fn swap(&mut self, other: &mut RbNodeBase<K, I, NA>) {
        core::mem::swap(&mut self.parent, &mut other.parent);
        core::mem::swap(&mut self.left, &mut other.left);
        core::mem::swap(&mut self.right, &mut other.right);
        core::mem::swap(&mut self.augment, &mut other.augment);
    }

    pub fn insert<'a, TA>(
        &mut self,
        tree: &mut RbTreeBase<K, I, TA, NA>,
        mut new_node: NonNull<RbNodeBase<K, I, NA>>,
    ) where
        K: Ord + Sized,
        RbNodeBase<K, I, NA>: Augment + AugmentLink<K, I, NA>,
    {
        let key = unsafe { &new_node.as_ref().key };
        let (mut parent, ordering) = self
            .search_closest_and_recalc(key, K::cmp)
            .expect("Insert to a NULL RbNode!");

        let new_node = unsafe { new_node.as_mut() };
        let parent_ref = unsafe { parent.as_mut() };

        parent_ref.link_node(new_node, ordering);
        new_node.insert_fixup(tree);

        parent_ref.link_ext(new_node, ordering);
    }
}

impl<K: Sized> RbNodeBase<K, (), ()> {
    pub const fn new(key: K) -> Self {
        Self::_new(key, ())
    }
}

impl<K: Sized, I, A> RbNodeBase<K, I, A> {
    const fn _new(key: K, augment: A) -> Self {
        RbNodeBase {
            parent: RbNodeWithColor::null_node(),
            left: None,
            right: None,
            key,
            augment,
            _phantom: PhantomData,
        }
    }

    /// 将`new_node`连接为当前节点的子节点，依据`order`指定左右位置
    /// - `order`: `Less`/`Equal` 表示作为左子节点，`Greater` 表示作为右子节点
    pub fn link_node(&mut self, new_node: &mut RbNodeBase<K, I, A>, order: cmp::Ordering) {
        match order {
            cmp::Ordering::Less | cmp::Ordering::Equal => {
                self.left = Some(NonNull::from_mut(new_node));
            }
            cmp::Ordering::Greater => {
                self.right = Some(NonNull::from_mut(new_node));
            }
        }
        new_node.parent = RbNodeWithColor::new(NonNull::from_mut(self), RbColor::Red);
    }

    pub fn search_closest_and_recalc<'a, Compare>(
        &self,
        key: &K,
        cmp: Compare,
    ) -> Option<(NonNull<RbNodeBase<K, I, A>>, cmp::Ordering)>
    where
        Compare: Fn(&K, &K) -> cmp::Ordering,
        A: 'a,
        K: 'a,
        I: 'a,
    {
        let mut current = Some(NonNull::from_ref(self));
        let mut result = None;

        while let Some(mut node_ptr) = current {
            unsafe {
                let node = node_ptr.as_mut();
                let ordering = cmp(&key, &node.key);
                result = Some((node_ptr, ordering));

                match ordering {
                    cmp::Ordering::Equal => {
                        return result;
                    }
                    cmp::Ordering::Less => {
                        current = node.left;
                    }
                    cmp::Ordering::Greater => {
                        current = node.right;
                    }
                }
            }
        }

        result
    }
}

impl<K: Sized, I, A, Compare: Fn(&K, &K) -> cmp::Ordering> RbSearch<K, RbNodeBase<K, I, A>, Compare>
    for RbNodeBase<K, I, A>
{
    /// 找到与`key`最接近的节点
    /// 返回值包含节点指针及其与`key`的比较结果
    /// - 如果找到完全相等的节点，返回`Ordering::Equal`
    /// - 如果未找到完全相等的节点，返回最后访问节点及其与`key`的比较结果
    /// - 如果是无法比较的类型，则视为`Greater`
    fn search_closest(
        &self,
        key: &K,
        cmp: Compare,
    ) -> Option<(NonNull<RbNodeBase<K, I, A>>, cmp::Ordering)> {
        self.search_closest_and_recalc(key, cmp)
    }
}

impl<K: Sized, I, TA, NA, Compare: Fn(&K, &K) -> cmp::Ordering>
    RbSearch<K, RbNodeBase<K, I, NA>, Compare> for RbTreeBase<K, I, TA, NA>
{
    fn search_closest(
        &self,
        key: &K,
        cmp: Compare,
    ) -> Option<(NonNull<RbNodeBase<K, I, NA>>, cmp::Ordering)> {
        if let Some(root_ptr) = self.root {
            unsafe { root_ptr.as_ref().search_closest(key, cmp) }
        } else {
            None
        }
    }
}

pub fn relation_assert<K: Sized, I, A>(node: &RbNodeBase<K, I, A>) {
    if let Some(left) = node.left {
        let left_ref = unsafe { left.as_ref() };
        assert_eq!(
            Some(NonNull::from_ref(node)),
            left_ref.get_parent(),
            "Left child parent pointer incorrect!"
        );
        relation_assert(left_ref);
    }

    if let Some(right) = node.right {
        let right_ref = unsafe { right.as_ref() };
        assert_eq!(
            Some(NonNull::from_ref(node)),
            right_ref.get_parent(),
            "Right child parent pointer incorrect!"
        );
        relation_assert(right_ref);
    }
}

pub fn validation<I, A>(
    parent: NonNull<RbNodeBase<usize, I, A>>,
    node: Option<NonNull<RbNodeBase<usize, I, A>>>,
    mut black_height: u32,
) -> u32 {
    if let Some(node) = node {
        let _node = unsafe { node.as_ref() };

        assert_eq!(parent, _node.get_parent().unwrap());

        if let RbColor::Black = _node.get_color() {
            black_height += 1;
        }

        if let RbColor::Red = _node.get_color()
            && let RbColor::Red = unsafe { parent.as_ref() }.get_color()
        {
            panic!("Red node has red parent!");
        }

        let left_black_height = validation(node, _node.left, black_height);
        let right_black_height = validation(node, _node.right, black_height);

        assert_eq!(left_black_height, right_black_height);
        left_black_height
    } else {
        black_height + 1
    }
}
