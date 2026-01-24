use core::{
    cell::SyncUnsafeCell, cmp::Ordering, mem::MaybeUninit, num::NonZeroUsize, pin::Pin,
    ptr::NonNull,
};

use crate::{
    container_of,
    kernel::memory::{
        MemoryError,
        phy::kmalloc::kmalloc,
        vir::page::{PageNumber, VirtPages, VmRange},
    },
    lib::rust::{
        list::{ListHead, ListNode},
        rbtree::linked::{Linked, LinkedRbNodeBase, LinkedRbTreeBase},
        spinlock::Spinlock,
    },
    linked_augment, list_first_owner,
};

const MAX_VMAP_POOL_PAGES: usize = 256;

pub(super) struct VmapPool {
    pub(super) list_head: Spinlock<ListHead<LinkedRbNodeBase<VmRange, usize>>>,
}

pub(super) struct VmapNode {
    pub(super) pools: [VmapPool; MAX_VMAP_POOL_PAGES],

    pub(super) allocated: Spinlock<LinkedRbTreeBase<VmRange, (), usize>>,
}

static VMAP_NODE: SyncUnsafeCell<MaybeUninit<VmapNode>> =
    SyncUnsafeCell::new(MaybeUninit::zeroed());
static mut FREE_VMAP_TREE: Spinlock<LinkedRbTreeBase<VmRange, (), usize>> =
    Spinlock::new(LinkedRbTreeBase::empty());

pub(super) fn get_vmap_node<'a>() -> &'a mut VmapNode {
    unsafe { (*VMAP_NODE.get()).assume_init_mut() }
}

impl VmapNode {
    pub(super) fn init(&mut self) {
        #[allow(static_mut_refs)]
        unsafe {
            FREE_VMAP_TREE.init_with(|rbtree| {
                rbtree.init();

                let (start, end) = (0xe0000000, 0xff800000);
                let start = PageNumber::from_addr(start).unwrap();
                let end = PageNumber::from_addr(end).unwrap();

                let vm_range = VmRange { start, end };

                let mut vpages =
                    kmalloc::<VirtPages>(NonZeroUsize::new_unchecked(size_of::<VirtPages>()))
                        .expect("Allocate slub memory failed in VmapNode::init()!");

                vpages.write(VirtPages::new(vm_range));
                rbtree.insert(&mut vpages.as_mut().rb_node);
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

    fn pool_put(&mut self, vpages: &mut VirtPages) {
        let count = vpages.rb_node.get_key().get_count();
        if count >= MAX_VMAP_POOL_PAGES {
            return;
        }
        let pool = unsafe { self.pools.get_unchecked_mut(count) };

        let mut list_head = pool.list_head.lock();
        let mut list_head = unsafe { Pin::new_unchecked(&mut *list_head) };
        let node = unsafe { Pin::new_unchecked(&mut vpages.rb_node.augment.list_node) };

        list_head.add_tail(node);
    }

    fn pool_get(&mut self, count: NonZeroUsize) -> Option<NonNull<VirtPages>> {
        let index = count.get() - 1;
        if index >= MAX_VMAP_POOL_PAGES {
            return None;
        }

        let pool = unsafe { self.pools.get_unchecked_mut(index) };
        if pool.list_head.get_relaxed().is_empty() {
            return None;
        }

        let mut list_head = pool.list_head.lock();
        let list_head = unsafe { Pin::new_unchecked(&mut *list_head) };

        let mut linked_node = list_first_owner!(Linked<VmRange, ()>, list_node, list_head)
            .expect("List is empty after checked!");

        // 通过 linked_node -> rbnode -> vpages 的层级关系获取 vpages
        let rb_node = container_of!(linked_node, LinkedRbNodeBase<VmRange, usize>, augment);
        let mut vpages = container_of!(rb_node, VirtPages, rb_node);

        let actual_count = unsafe { rb_node.as_ref() }.get_key().get_count();
        match actual_count.cmp(&count.get()) {
            Ordering::Less => None,
            Ordering::Equal => {
                unsafe { Pin::new_unchecked(&mut linked_node.as_mut().list_node) }.del();
                Some(vpages)
            }
            Ordering::Greater => {
                unsafe { vpages.as_mut().split_to_pool(count, list_head) }
                    .expect("Allocate slub memory failed in VmapNode::pool_get()!");
                Some(vpages)
            }
        }
    }

    pub fn allocate(&mut self, count: NonZeroUsize) -> Result<NonNull<VirtPages>, MemoryError> {
        // 先从快速池获取
        let mut vpages = self
            .pool_get(count)
            .or_else(|| self.allocate_from_tree(count))
            .ok_or(MemoryError::OutOfMemory)?;

        // 加入已分配树
        self.allocated
            .lock()
            .insert(&mut unsafe { vpages.as_mut() }.rb_node);
        Ok(vpages)
    }

    /// 从红黑树中查找并分配满足条件的虚拟页块
    /// 查找策略：优先左子树（smaller but sufficient），精确匹配或分割
    fn allocate_from_tree(&mut self, count: NonZeroUsize) -> Option<NonNull<VirtPages>> {
        #[allow(static_mut_refs)]
        let tree = unsafe { FREE_VMAP_TREE.lock() };
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
                let mut vpages = container_of!(node, VirtPages, rb_node);

                if node_count > count.get() {
                    // 需要分割：从 vpages 中切出 count 个页，剩余部分重新插入树
                    self.split_vpages(&mut vpages, count)?;
                }

                return Some(vpages);
            }

            // 当前节点不够大，搜索右子树
            node = node_ref
                .right
                .expect("Augmented RB-tree invariant violated: child max < parent size");
        }
    }

    /// 分割虚拟页块：从 vpages 中切出 count 个页，剩余部分重新插入树
    /// 优化：直接修改原节点为剩余部分，避免删除/重新插入节点
    fn split_vpages(&mut self, vpages: &mut NonNull<VirtPages>, count: NonZeroUsize) -> Option<()> {
        let vpages_ref = unsafe { vpages.as_mut() };
        let old_start = vpages_ref.rb_node.get_key().start;

        // 计算分割点
        let split_point = old_start.get().get() + count.get();

        // 分配新节点存储分配部分 [old_start, split_point - 1]
        let allocated_part =
            kmalloc::<VirtPages>(unsafe { NonZeroUsize::new_unchecked(size_of::<VirtPages>()) })?;

        // Safety
        // 1. vpages_ref 在 tree 中已有节点，修改其范围不会违反 RB-tree 结构
        // 2. split_point > old_start 保证不会出现无效范围
        unsafe {
            // 初始化分配部分
            allocated_part.write(VirtPages::new(VmRange {
                start: old_start,
                end: PageNumber::new(NonZeroUsize::new_unchecked(split_point - 1)),
            }));

            // 修改原节点为剩余部分 [split_point, old_end]
            vpages_ref.rb_node.get_key_mut().start =
                PageNumber::new(NonZeroUsize::new_unchecked(split_point));
        }

        // 更新 vpages 指针指向分配部分（返回值）
        *vpages = allocated_part;

        Some(())
    }

    pub fn deallocate(&mut self, range: &VmRange) -> Result<(), MemoryError> {
        let mut node = self
            .allocated
            .lock()
            .delete(&range)
            .ok_or(MemoryError::DoubleRelease)?;
        let mut vpages = container_of!(node, VirtPages, rb_node);

        let node = unsafe { node.as_mut() };
        if node.get_key().get_count() >= MAX_VMAP_POOL_PAGES {
            #[allow(static_mut_refs)]
            unsafe {
                FREE_VMAP_TREE.lock().insert(node);
            }
        } else {
            self.pool_put(unsafe { vpages.as_mut() });
        }

        Ok(())
    }
}
