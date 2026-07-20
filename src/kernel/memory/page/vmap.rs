use core::{mem, num::NonZeroUsize, ops::DerefMut, pin::Pin, ptr::NonNull};

use alloc::boxed::Box;

use crate::{
    container_of,
    kernel::memory::{
        MemoryError,
        kmalloc::{Atomic, Kmalloc},
        page::{dyn_pages::DynPages, range::VmRange},
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
                    DynPages::kernel(),
                    Kmalloc::<Atomic>::default(),
                ));

                free_map_tree().as_mut().insert(&mut pages.rb_node);
            })
        };

        unsafe {
            let pools = &mut self.as_mut().get_unchecked_mut().pools;
            for pool in pools.iter_mut() {
                pool.list_head
                    .init_with(|list_head| Pin::new_unchecked(list_head).init());
            }
            self.allocated.init_with(|rbtree| rbtree.init());
        }
    }

    fn pool_put(self: &Pin<&mut Self>, pages: &mut DynPages) {
        let count = pages.rb_node.get_key().get_count();
        if count >= MAX_VMAP_POOL_PAGES {
            return;
        }
        let mut list_head = unsafe {
            self.as_ref()
                .map_unchecked(|vmap| &vmap.pools.get_unchecked(count).list_head)
        }
        .lock_pinned();

        let node = pages.rb_node.augment.get_list();

        list_head.add_tail(node);
    }

    fn pool_get(self: &Pin<&mut Self>, count: NonZeroUsize) -> Option<Box<DynPages, Kmalloc>> {
        let index = count.get() - 1;
        if index >= MAX_VMAP_POOL_PAGES {
            return None;
        }

        let pool = unsafe { self.pools.get_unchecked(index) };
        if pool.list_head.get_relaxed().is_empty() {
            return None;
        }

        let mut list_head = unsafe {
            self.as_ref()
                .map_unchecked(|vmap| &vmap.pools.get_unchecked(index).list_head)
        }
        .lock_pinned();

        let mut rb_node = list_head
            .as_ref()
            .iter(RbTree::linked_offset())
            .next()
            .expect("List is empty after checked!");

        // 通过 linked_node -> rbnode -> pages 的层级关系获取 pages
        let pages = container_of!(rb_node, DynPages, rb_node);

        unsafe {
            let mut list_head = Pin::new_unchecked(list_head.deref_mut());
            list_head.delete(rb_node.as_mut().augment.get_list());
        }
        Some(unsafe { Box::from_non_null_in(pages, Kmalloc::default()) })
    }

    pub fn allocate(
        self: Pin<&mut Self>,
        count: NonZeroUsize,
    ) -> Result<Box<DynPages, Kmalloc>, MemoryError> {
        // 先从快速池获取
        let mut pages = self
            .pool_get(count)
            .or_else(|| self.allocate_from_tree(count))
            .ok_or(MemoryError::OutOfMemory)?;

        // 加入已分配树
        unsafe { self.as_ref().map_unchecked(|vmap| &vmap.allocated) }
            .lock_pinned()
            .as_mut()
            .insert(&mut pages.rb_node);
        Ok(pages)
    }

    /// 从红黑树中查找并分配满足条件的虚拟页块
    /// 查找策略：优先左子树（smaller but sufficient），精确匹配或分割
    fn allocate_from_tree(
        self: &Pin<&mut Self>,
        count: NonZeroUsize,
    ) -> Option<Box<DynPages, Kmalloc>> {
        let tree = free_map_tree();
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
                let mut pages = container_of!(node, DynPages, rb_node);

                return if node_count > count.get() {
                    // 需要分割：从 pages 中切出 count 个页，剩余部分重新插入树
                    unsafe { pages.as_mut().split(count) }
                } else {
                    free_map_tree().as_mut().delete_node(node);
                    Some(unsafe { Box::from_non_null_in(pages, Kmalloc::default()) })
                };
            }

            // 当前节点不够大，搜索右子树
            node = node_ref
                .right
                .expect("Augmented RB-tree invariant violated: child max < parent size");
        }
    }

    pub fn search_allocated(self: &Pin<&mut Self>, range: &VmRange) -> Option<NonNull<DynPages>> {
        let node = unsafe { self.as_ref().map_unchecked(|vmap| &vmap.allocated) }
            .lock_pinned()
            .as_ref()
            .search_exact(range, VmRange::cmp)?;

        let pages = container_of!(node, DynPages, rb_node);
        Some(pages)
    }

    pub fn deallocate(self: &Pin<&mut Self>, pages: &mut DynPages) -> Result<(), MemoryError> {
        unsafe { self.as_ref().map_unchecked(|vmap| &vmap.allocated) }
            .lock_pinned()
            .as_mut()
            .delete_node(NonNull::from(&pages.rb_node));

        let node = &mut pages.rb_node;

        if node.get_key().get_count() >= MAX_VMAP_POOL_PAGES {
            free_map_tree().as_mut().insert(node);
        } else {
            self.pool_put(pages);
        }

        Ok(())
    }
}

fn free_map_tree<'a>() -> SpinGuard<'a, Pin<&'a mut RbTree>> {
    unsafe { Pin::new_unchecked(&FREE_VMAP_TREE) }.lock_pinned()
}
