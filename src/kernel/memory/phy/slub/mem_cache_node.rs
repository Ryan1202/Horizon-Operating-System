use core::{num::NonZeroU16, ptr::NonNull};

use crate::{
    kernel::memory::phy::{
        page::{Page, ZoneType, buddy::PageOrder, page_align_down},
        slub::{ObjectSize, Slub, SlubType, calculate_sizes, mem_cache::MemCache},
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
    list_for_each_owner,
};

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    pub partial_list: Spinlock<ListHead<Slub>>,
    object_size: ObjectSize,
    object_num: NonZeroU16,
    order: PageOrder,
    zone_type: ZoneType,
}

impl MemCacheNode {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(size_of::<Self>() as u16).unwrap();

    fn init(
        &mut self,
        object_size: ObjectSize,
        object_num: NonZeroU16,
        order: PageOrder,
        slub: Option<NonNull<Slub>>,
        zone_type: ZoneType,
    ) {
        *self = Self {
            partial_list: Spinlock::new(ListHead::empty()),
            object_num,
            object_size,
            order,
            zone_type,
        };

        let mut partial_list = self.partial_list.lock();
        partial_list.init();
        if let Some(mut slub) = slub {
            unsafe {
                let slub = slub.as_mut();
                if let SlubType::Head(_) = &mut slub.slub_type {
                    partial_list.add_head(&mut slub.list);
                } else {
                    panic!("MemCacheNode::init() called with a Slub that is not SlubType::Head!");
                }
            }
        }
    }

    /// 创建一个Slub存放自身，作为 MemCacheNode 类型的 MemCache 的一个节点
    pub(super) fn bootstrap() -> NonNull<Self> {
        let (object_size, object_num, order) = calculate_sizes(Self::OBJECT_SIZE).unwrap();

        let mut slub = Slub::new(ZoneType::LinearMem, object_size, object_num, order).unwrap();

        unsafe {
            let _slub = slub.as_mut();
            let mut mem_cache_node: NonNull<Self> = match &mut _slub.slub_type {
                SlubType::Head(head) => head.allocate().unwrap(),
                _ => unreachable!(),
            };

            mem_cache_node.as_mut().init(
                object_size,
                object_num,
                order,
                Some(slub),
                ZoneType::LinearMem,
            );

            mem_cache_node
        }
    }

    /// 从已创建的 MemCacheNode 创建 "mem_cache_node" 的 MemCache
    pub fn bootstrap_cache(ptr: NonNull<Self>, mem_cache: &mut MemCache) -> NonNull<MemCache> {
        const NAME: *const u8 = b"mem_cache_node\0".as_ptr();

        mem_cache.new_boot_from_node(NAME, ptr, Self::OBJECT_SIZE)
    }

    // 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    pub fn new(&mut self, object_size: NonZeroU16) -> Option<NonNull<MemCacheNode>> {
        let (object_size, object_num, order) =
            calculate_sizes(NonZeroU16::new(object_size.get())?).unwrap();
        let mut result = self.allocate::<MemCacheNode>()?;

        unsafe {
            let slub = Slub::new(ZoneType::LinearMem, object_size, object_num, order).ok()?;

            result
                .as_mut()
                .init(object_size, object_num, order, Some(slub), self.zone_type);
        }

        Some(result)
    }

    /// 从`Node`中分配对象
    ///
    /// 默认从`partial_list`中分配对象，若未分配到则尝试创建一个新的`Slub`并从中分配
    ///
    /// 新的`Slub`分配失败时返回`None`
    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        if !self.partial_list.get_atomic_snapshot().is_empty() {
            let mut partial_list = self.partial_list.lock();

            list_for_each_owner!(slub_head, Slub, list, &mut partial_list, {
                let inner = &mut unsafe { slub_head.as_mut() }.slub_type;
                if let SlubType::Head(head) = inner {
                    if let Some(obj) = head.allocate::<T>() {
                        return Some(obj);
                    }
                } else {
                    panic!("Slub in MemCacheNode::partial_list is not SlubType::Head!");
                }
            });
        }

        // 从partial_list中没有分配到，创建一个新的Slub
        let mut new_slub = Slub::new(
            self.zone_type,
            self.object_size,
            self.object_num,
            self.order,
        )
        .ok()?;

        let new_slub = unsafe { new_slub.as_mut() };
        if let SlubType::Head(new_head) = &mut new_slub.slub_type {
            self.partial_list.lock().add_head(&mut new_slub.list);
            new_head.allocate()
        } else {
            panic!("Slub::new() returns a SlubType::Body slub!");
        }
    }

    pub fn free<T>(&mut self, obj: NonNull<T>) -> Option<()> {
        // 先找到所在页的 Page 结构，再从其中取出 Slub 地址
        let page = unsafe {
            obj.map_addr(|addr| {
                let page_start = page_align_down(addr.get());
                Page::from_addr(page_start).addr()
            })
            .cast::<Page>()
            .as_mut()
        };

        if let Page::Slub(slub) = page {
            let head = match &mut slub.slub_type {
                SlubType::Body(head) => unsafe {
                    // 如果是Body就重新定位到Head
                    match &mut head.as_mut().slub_type {
                        SlubType::Head(head) => head,
                        _ => unreachable!(),
                    }
                },
                SlubType::Head(head) => head,
            };

            head.free(obj);
        }

        Some(())
    }
}
