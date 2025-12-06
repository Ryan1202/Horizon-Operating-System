use core::{
    cell::SyncUnsafeCell, cmp::Ordering, mem::MaybeUninit, num::NonZeroUsize, ptr::NonNull,
};

use crate::{
    container_of,
    kernel::memory::{
        phy::{kmalloc::kmalloc, page::ZoneType},
        vir::page::{VirtPageNumber, VirtPages, VmRange},
    },
    lib::rust::{
        list::{ListHead, ListNode},
        rbtree::linked::{Linked, LinkedRbNodeBase, LinkedRbTree, LinkedRbTreeBase},
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

    pub(super) linked_rbtree: Spinlock<LinkedRbTree<VmRange>>,
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
                let start = VirtPageNumber::from_addr(start).unwrap();
                let end = VirtPageNumber::from_addr(end).unwrap();

                let vm_range = VmRange { start, end };

                let mut vpages =
                    kmalloc::<VirtPages>(NonZeroUsize::new_unchecked(size_of::<VirtPages>()))
                        .expect("Allocate slub memory failed in VmapNode::init()!");

                vpages.write(VirtPages::new(vm_range));
                rbtree.insert(&mut vpages.as_mut().rb_node);
            })
        };

        for pool in self.pools.iter_mut() {
            pool.list_head.init_with(|list_head| list_head.init());
        }
        self.linked_rbtree.init_with(|rbtree| rbtree.init());
    }

    fn pool_put(&mut self, vpages: &mut VirtPages) {
        let count = vpages.rb_node.get_key().get_count();
        if count >= MAX_VMAP_POOL_PAGES {
            return;
        }
        let pool = unsafe { self.pools.get_unchecked_mut(count) };
        let mut list_head = pool.list_head.lock();
        list_head.add_tail(&mut vpages.rb_node.augment.list_node);
    }

    fn pool_get(&mut self, count: NonZeroUsize) -> Option<NonNull<VirtPages>> {
        let index = count.get() - 1;
        if index >= MAX_VMAP_POOL_PAGES {
            return None;
        }
        let pool = unsafe { self.pools.get_unchecked_mut(index) };
        let list_head = pool.list_head.get_relaxed();
        if list_head.is_empty() {
            return None;
        } else {
            let mut list_head = pool.list_head.lock();
            let mut node = list_first_owner!(Linked<VmRange, ()>, list_node, list_head)
                .expect("List is empty after checked!");
            let rbnode = container_of!(node, LinkedRbNodeBase<VmRange, usize>, augment);

            let node_count = unsafe { rbnode.as_ref() }.get_key().get_count();
            match node_count.cmp(&count.get()) {
                Ordering::Less => None,
                Ordering::Equal => {
                    let vpages = container_of!(rbnode, VirtPages, rb_node);
                    unsafe { node.as_mut() }.list_node.del();
                    Some(vpages)
                }
                Ordering::Greater => {
                    let mut vpages = container_of!(rbnode, VirtPages, rb_node);
                    let vpages_ref = unsafe { vpages.as_mut() };

                    unsafe { vpages_ref.split_to_pool(count, &mut list_head) }
                        .expect("Allocate slub memory failed in VmapNode::pool_get()!");

                    Some(vpages)
                }
            }
        }
    }

    pub fn allocate(&mut self, count: NonZeroUsize) -> Option<NonNull<VirtPages>> {
        let vpages = self.pool_get(count).or_else(|| {
            #[allow(static_mut_refs)]
            let mut tree = unsafe { FREE_VMAP_TREE.lock() };
            let mut node = tree.root?;

            if (unsafe { node.as_ref() }).get_key().get_count() < count.get() {
                return None;
            }
            loop {
                let node_ref = unsafe { node.as_mut() };
                let left = node_ref.left;

                if let Some(left_ref) = left
                    && linked_augment!(unsafe { left_ref.as_ref() }) >= count.get()
                {
                    // 左子树还有更合适的节点
                    node = left_ref;
                    continue;
                }

                let _count = node_ref.get_key().get_count();
                if _count >= count.get() {
                    let mut vpages = container_of!(node, VirtPages, rb_node);

                    if _count > count.get() {
                        let vpages_ref = unsafe { vpages.as_mut() };

                        let mut new_vpages = kmalloc::<VirtPages>(unsafe {
                            NonZeroUsize::new_unchecked(size_of::<VirtPages>())
                        })?;

                        vpages_ref.rb_node.delete(&mut tree);
                        unsafe {
                            let new_vpages_ref = new_vpages.as_mut();
                            new_vpages.write(VirtPages::new(VmRange {
                                start: VirtPageNumber::new(NonZeroUsize::new_unchecked(
                                    vpages_ref.rb_node.get_key().start.get().get() + count.get(),
                                )),
                                end: vpages_ref.rb_node.get_key().end,
                            }));
                            tree.insert(&mut new_vpages_ref.rb_node);
                            vpages_ref.rb_node.get_key_mut().end =
                                VirtPageNumber::new(NonZeroUsize::new_unchecked(
                                    vpages_ref.rb_node.get_key().start.get().get() + count.get()
                                        - 1,
                                ));
                        }
                    }
                    return Some(vpages);
                }

                node = node_ref
                    .right
                    .expect("Max size of childs < count when max size of the node >= count");
            }
        })?;

        Some(vpages)
    }

    pub fn deallocate(&mut self, vpages: &mut VirtPages) {
        if vpages.rb_node.get_key().get_count() >= MAX_VMAP_POOL_PAGES {
            #[allow(static_mut_refs)]
            unsafe {
                FREE_VMAP_TREE.lock().insert(&mut vpages.rb_node);
            }
        } else {
            self.pool_put(vpages);
        }
    }
}
