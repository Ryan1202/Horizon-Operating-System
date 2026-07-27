use core::{mem, num::NonZeroUsize, pin::Pin, ptr::NonNull};

use alloc::boxed::Box;

use crate::{
    container_of,
    kernel::memory::{
        MemoryError,
        kmalloc::{Atomic, Kmalloc},
        page::{
            dyn_pages::{DynPages, VmapNode},
            range::VmRange,
        },
    },
    lib::rust::{
        list::ListHead,
        rbtree::{
            RbSearch,
            linked::{LinkedRbNodeBase, LinkedRbTreeBase},
        },
        spinlock::{SpinGuard, Spinlock},
    },
    linked_augment,
};

const MAX_VMAP_POOL_PAGES: usize = 256;

type RbTree = LinkedRbTreeBase<VmRange, (), usize>;
type RbNode = LinkedRbNodeBase<VmRange, usize>;

pub(super) struct VmapPool {
    pub(super) list_head: Spinlock<ListHead<RbNode>>,
}

pub struct Vmap {
    pub(super) pools: [VmapPool; MAX_VMAP_POOL_PAGES],

    pub(super) allocated: Spinlock<RbTree>,
}

static VMAP: Spinlock<Vmap> = Spinlock::new(unsafe { mem::zeroed() });
static FREE_VMAP_TREE: Spinlock<RbTree> = Spinlock::new(LinkedRbTreeBase::empty());

pub fn get_vmap<'a>() -> SpinGuard<'a, Pin<&'a mut Vmap>> {
    unsafe { Pin::new_unchecked(&VMAP).lock_pinned() }
}

impl Vmap {
    pub fn init(mut self: Pin<&mut Self>) {
        unsafe {
            FREE_VMAP_TREE.init_with(|rbtree| {
                rbtree.init();

                let pages = Box::leak(Box::new_in(
                    VmapNode::kernel(),
                    Kmalloc::<Atomic>::default(),
                ));

                free_map_tree().as_mut().insert(&mut pages.rb_node);
            })
        };

        unsafe {
            let pools = &mut self.as_mut().get_unchecked_mut().pools;
            for pool in pools.iter_mut() {
                pool.list_head.init_with(|list_head| list_head.init());
            }
            self.allocated.init_with(|rbtree| rbtree.init());
        }
    }

    fn pool_index(count: NonZeroUsize) -> Option<usize> {
        let index = count.get() - 1;
        (index < MAX_VMAP_POOL_PAGES).then_some(index)
    }

    fn pool_put(&self, node: &mut VmapNode) {
        let count = NonZeroUsize::new(node.rb_node.get_key().get_count()).unwrap();
        let index = Self::pool_index(count).expect("oversized VmapNode passed to pool_put");

        let mut list_head =
            unsafe { Pin::new_unchecked(&self.pools.get_unchecked(index).list_head) }.lock_pinned();

        let node = node.rb_node.augment.get_list();

        unsafe { list_head.as_mut().get_unchecked_mut() }.add_tail(node);
    }

    fn pool_get(self: &Pin<&mut Self>, count: NonZeroUsize) -> Option<NonNull<VmapNode>> {
        let index = Self::pool_index(count)?;

        let pool = unsafe { self.pools.get_unchecked(index) };
        if pool.list_head.get_relaxed().is_empty() {
            return None;
        }

        let mut guard =
            unsafe { Pin::new_unchecked(&self.pools.get_unchecked(index).list_head) }.lock_pinned();

        let list_head = unsafe { guard.as_mut().get_unchecked_mut() };

        let mut rb_node = list_head
            .iter(RbTree::linked_offset())
            .next()
            .expect("List is empty after checked!");

        // 通过 linked_node -> rbnode -> pages 的层级关系获取 pages
        let pages = container_of!(rb_node, VmapNode, rb_node);

        unsafe {
            list_head.delete(rb_node.as_mut().augment.get_list());
        }
        Some(pages)
    }

    pub fn allocate(self: Pin<&mut Self>, count: NonZeroUsize) -> Result<DynPages, MemoryError> {
        // 先从快速池获取
        let mut pages = self
            .pool_get(count)
            .or_else(|| self.allocate_from_tree(count))
            .ok_or(MemoryError::OutOfMemory)?;

        // 加入已分配树
        unsafe { self.as_ref().map_unchecked(|vmap| &vmap.allocated) }
            .lock_pinned()
            .as_mut()
            .insert(unsafe { &mut pages.as_mut().rb_node });

        Ok(unsafe { DynPages::new(pages) })
    }

    /// 从红黑树中查找并分配满足条件的虚拟页块
    /// 查找策略：优先左子树（smaller but sufficient），精确匹配或分割
    fn allocate_from_tree(self: &Pin<&mut Self>, count: NonZeroUsize) -> Option<NonNull<VmapNode>> {
        let mut tree = free_map_tree();
        let mut node = tree.root?;

        // 根节点不满足要求，整棵树都不够大
        if linked_augment!(unsafe { node.as_ref() }) < count.get() {
            return None;
        }

        // 在红黑树中查找最合适的节点（优先左子树的小块）
        loop {
            let node_ref = unsafe { node.as_mut() };

            // 如果左子树存在且最大值满足需求，优先搜索左子树
            if let Some(left) = node_ref.left
                && linked_augment!(unsafe { left.as_ref() }) >= count.get()
            {
                node = left;
                continue;
            }

            // 当前节点满足需求
            let node_count = node_ref.get_key().get_count();
            if node_count >= count.get() {
                let mut pages = container_of!(node, VmapNode, rb_node);

                return if node_count > count.get() {
                    tree.as_mut().delete_node(node);
                    let allocated = unsafe { pages.as_mut().split(count) };
                    tree.as_mut().insert(unsafe { &mut pages.as_mut().rb_node });

                    allocated.map(|node| NonNull::from_mut(Box::leak(node)))
                } else {
                    tree.as_mut().delete_node(node);
                    Some(pages)
                };
            }

            // 当前节点不够大，搜索右子树
            node = node_ref
                .right
                .expect("Augmented RB-tree invariant violated: child max < parent size");
        }
    }

    pub(in crate::kernel::memory) fn search_allocated(
        self: &Pin<&mut Self>,
        range: &VmRange,
    ) -> Option<NonNull<VmapNode>> {
        let node = unsafe { self.as_ref().map_unchecked(|vmap| &vmap.allocated) }
            .lock_pinned()
            .as_ref()
            .search_exact(range, VmRange::cmp)?;

        let node = container_of!(node, VmapNode, rb_node);
        Some(node)
    }

    pub(in crate::kernel::memory) fn deallocate(
        self: &Pin<&mut Self>,
        pages: &mut VmapNode,
    ) -> Result<(), MemoryError> {
        debug_assert_eq!(
            pages.frame_count, 0,
            "releasing a VmapNode with mapped frames"
        );

        unsafe { self.as_ref().map_unchecked(|vmap| &vmap.allocated) }
            .lock_pinned()
            .as_mut()
            .delete_node(NonNull::from(&pages.rb_node));

        let count = NonZeroUsize::new(pages.rb_node.get_key().get_count()).unwrap();

        if Self::pool_index(count).is_some() {
            self.pool_put(pages);
        } else {
            free_map_tree().as_mut().insert(&mut pages.rb_node);
        }
        Ok(())
    }
}

fn free_map_tree<'a>() -> SpinGuard<'a, Pin<&'a mut RbTree>> {
    unsafe { Pin::new_unchecked(&FREE_VMAP_TREE) }.lock_pinned()
}
