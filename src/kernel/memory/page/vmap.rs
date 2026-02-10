use core::{
    cell::SyncUnsafeCell,
    cmp::Ordering,
    mem::{self, MaybeUninit, offset_of},
    num::NonZeroUsize,
    ops::DerefMut,
    pin::Pin,
    ptr::NonNull,
};

use crate::{
    container_of,
    kernel::memory::{
        MemoryError,
        kmalloc::kmalloc,
        page::{dyn_pages::DynPages, range::VmRange},
    },
    lib::rust::{
        list::ListHead,
        rbtree::{
            RbSearch,
            linked::{Linked, LinkedRbNodeBase, LinkedRbTreeBase},
        },
        spinlock::{SpinGuard, Spinlock},
    },
    linked_augment,
};

const MAX_VMAP_POOL_PAGES: usize = 256;

pub(super) struct VmapPool {
    pub(super) list_head: Spinlock<ListHead<LinkedRbNodeBase<VmRange, usize>>>,
}

pub struct VmapNode {
    pub(super) pools: [VmapPool; MAX_VMAP_POOL_PAGES],

    pub(super) allocated: Spinlock<LinkedRbTreeBase<VmRange, (), usize>>,
}

static VMAP_NODE: Spinlock<VmapNode> = Spinlock::new(unsafe { mem::zeroed() });
static mut FREE_VMAP_TREE: Spinlock<LinkedRbTreeBase<VmRange, (), usize>> =
    Spinlock::new(LinkedRbTreeBase::empty());

pub fn get_vmap_node<'a>() -> SpinGuard<'a, VmapNode> {
    VMAP_NODE.lock()
}

impl VmapNode {
    pub fn init(&mut self) {
        #[allow(static_mut_refs)]
        unsafe {
            FREE_VMAP_TREE.init_with(|rbtree| {
                rbtree.init();

                let mut pages =
                    kmalloc::<DynPages>(NonZeroUsize::new_unchecked(size_of::<DynPages>()))
                        .expect("Allocate slub memory failed in VmapNode::init()!");

                pages.write(DynPages::kernel());
                rbtree.insert(&mut pages.as_mut().rb_node);
            })
        };

        unsafe {
            for pool in self.pools.iter_mut() {
                pool.list_head
                    .init_with(|list_head| Pin::new_unchecked(list_head).init());
            }
            self.allocated.init_with(|rbtree| rbtree.init());
        }
    }

    fn pool_put(&mut self, pages: &mut DynPages) {
        let count = pages.rb_node.get_key().get_count();
        if count >= MAX_VMAP_POOL_PAGES {
            return;
        }
        let pool = unsafe { self.pools.get_unchecked_mut(count) };

        let mut list_head = pool.list_head.lock();
        let mut list_head = unsafe { Pin::new_unchecked(&mut *list_head) };
        let node = unsafe { Pin::new_unchecked(&mut pages.rb_node.augment.list_node) };

        list_head.add_tail(node);
    }

    fn pool_get(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
        let index = count.get() - 1;
        if index >= MAX_VMAP_POOL_PAGES {
            return None;
        }

        let pool = unsafe { self.pools.get_unchecked_mut(index) };
        if pool.list_head.get_relaxed().is_empty() {
            return None;
        }

        let mut list_head = pool.list_head.lock();

        let mut rb_node = list_head
            .iter(offset_of!(Linked<VmRange, ()>, list_node))
            .next()
            .expect("List is empty after checked!");

        // 通过 linked_node -> rbnode -> pages 的层级关系获取 pages
        let mut pages = container_of!(rb_node, DynPages, rb_node);

        let actual_count = unsafe { rb_node.as_ref() }.get_key().get_count();
        match actual_count.cmp(&count.get()) {
            Ordering::Less => None,
            Ordering::Equal => {
                unsafe { Pin::new_unchecked(&mut rb_node.as_mut().augment.list_node) }.del();
                Some(pages)
            }
            Ordering::Greater => {
                let pages = unsafe { pages.as_mut().split(count) }
                    .expect("Allocate slub memory failed in VmapNode::pool_get()!");
                Some(pages)
            }
        }
    }

    pub fn allocate(&mut self, count: NonZeroUsize) -> Result<NonNull<DynPages>, MemoryError> {
        // 先从快速池获取
        let mut pages = self
            .pool_get(count)
            .or_else(|| self.allocate_from_tree(count))
            .ok_or(MemoryError::OutOfMemory)?;

        // 加入已分配树
        self.allocated
            .lock()
            .insert(&mut unsafe { pages.as_mut() }.rb_node);
        Ok(pages)
    }

    /// 从红黑树中查找并分配满足条件的虚拟页块
    /// 查找策略：优先左子树（smaller but sufficient），精确匹配或分割
    fn allocate_from_tree(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
        #[allow(static_mut_refs)]
        let mut tree = unsafe { FREE_VMAP_TREE.lock() };
        let mut node = tree.root?;

        // 根节点不满足要求，整棵树都不够大
        if unsafe { node.as_ref() }.get_key().get_count() < count.get() {
            return None;
        }

        // 在红黑树中查找最合适的节点（优先左子树的小块）
        loop {
            let node_ref = unsafe { node.as_mut() };

            // 如果左子树存在且最大值满足需求，优先搜索左子树
            if let Some(left) = node_ref.left {
                if linked_augment!(unsafe { left.as_ref() }) >= count.get() {
                    node = left;
                    continue;
                }
            }

            // 当前节点满足需求
            let node_count = node_ref.get_key().get_count();
            if node_count >= count.get() {
                let mut pages = container_of!(node, DynPages, rb_node);

                return if node_count > count.get() {
                    // 需要分割：从 pages 中切出 count 个页，剩余部分重新插入树
                    unsafe { pages.as_mut().split(count) }
                } else {
                    node_ref.delete(tree.deref_mut());
                    Some(pages)
                };
            }

            // 当前节点不够大，搜索右子树
            node = node_ref
                .right
                .expect("Augmented RB-tree invariant violated: child max < parent size");
        }
    }

    pub fn search_allocated(&mut self, range: &VmRange) -> Option<NonNull<DynPages>> {
        let node = self.allocated.lock().search_exact(range, VmRange::cmp)?;

        let pages = container_of!(node, DynPages, rb_node);
        Some(pages)
    }

    pub fn deallocate(&mut self, pages: &mut DynPages) -> Result<(), MemoryError> {
        self.allocated
            .lock()
            .delete_node(NonNull::from(&pages.rb_node));

        let node = &mut pages.rb_node;

        if node.get_key().get_count() >= MAX_VMAP_POOL_PAGES {
            #[allow(static_mut_refs)]
            unsafe {
                FREE_VMAP_TREE.lock().insert(node);
            }
        } else {
            self.pool_put(pages);
        }

        Ok(())
    }
}
