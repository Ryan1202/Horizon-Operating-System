use core::{
    cell::SyncUnsafeCell,
    mem::MaybeUninit,
    num::{NonZero, NonZeroU16},
    ptr::NonNull,
};

use crate::{
    kernel::memory::{
        buddy::PageOrder,
        page::{page_align_down, ZoneType},
        slub::{
            calculate_sizes,
            config::{DEFAULT_CACHES, DEFAULT_CACHE_CONFIGS},
            Slub, SlubHead, SlubType, ALIGN, MAX_OBJECT_SIZE, MAX_PARTIAL, MIN_PARTIAL,
        },
        Page,
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::{Spinlock, SpinlockRaw},
    },
    list_for_each_owner,
};

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct ObjectSize(pub NonZeroU16);

impl ObjectSize {
    const unsafe fn new(size: u16) -> Self {
        Self(NonZeroU16::new_unchecked(
            size.next_multiple_of(ALIGN as u16),
        ))
    }

    pub const fn from<T: Sized>() -> ObjectSize {
        let size = size_of::<T>() as u16;
        assert!(size != 0, "size_of::<T>() == 0, NonZeroU16 required");
        // SAFETY: 已经断言 size != 0
        unsafe { Self::new(size) }
    }

    pub const fn from_u16(size: u16) -> ObjectSize {
        assert!(size != 0, "size == 0, NonZeroU16 required");
        debug_assert!((size as usize) < MAX_OBJECT_SIZE);
        // SAFETY: 已经断言 size != 0
        unsafe { Self::new(size) }
    }
}

#[repr(C)]
pub struct MemCaches {
    pub list_head: Spinlock<ListHead<MemCache>>,
    pub mem_cache: NonNull<MemCache>,
    pub mem_cache_node: NonNull<MemCache>,
}

unsafe impl Sync for MemCaches {}

impl MemCaches {
    pub fn init() {
        // 从零创建 MemCacheNode，对应 "mem_cache_node" 的 MemCacheNode
        let mem_cache_node = MemCacheNode::bootstrap();

        // 从mem_cache_node 创建 "mem_cache" 的 MemCacheNode，再从零创建 "mem_cache" 的 MemCache
        let mut mem_cache = MemCache::bootstrap(mem_cache_node);

        let mem_cache_node =
            unsafe { MemCacheNode::bootstrap_cache(mem_cache_node, mem_cache.as_mut()) };

        unsafe {
            let caches_uninit = &mut *CACHES.get();
            let caches = caches_uninit.write(MemCaches {
                list_head: Spinlock::new(ListHead::empty()),
                mem_cache,
                mem_cache_node,
            });

            let mem_cache_node = caches.mem_cache_node.as_mut();
            let mem_cache = caches.mem_cache.as_mut();

            for (i, cache) in DEFAULT_CACHE_CONFIGS.iter().enumerate() {
                DEFAULT_CACHES[i] = mem_cache.new_boot(
                    cache.name.as_ptr(),
                    mem_cache_node,
                    ObjectSize::from_u16(cache.object_size as u16),
                );
            }

            let mut list_head = caches.list_head.lock();
            list_head.init();
            list_head.add_head(&mut mem_cache_node.list);
            list_head.add_head(&mut mem_cache.list);

            for mut cache in DEFAULT_CACHES {
                list_head.add_tail(&mut cache.as_mut().list);
            }
        }
    }

    pub fn add_cache(&mut self, cache: &mut MemCache) {
        let mut list_head = self.list_head.lock();
        unsafe {
            list_head.add_head(&mut cache.list);
        }
    }
}

static CACHES: SyncUnsafeCell<MaybeUninit<MemCaches>> = SyncUnsafeCell::new(MaybeUninit::uninit());

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    slub: NonNull<Slub>,
    pub partial_list: Spinlock<ListHead<SlubHead>>,
}

impl MemCacheNode {
    const OBJECT_SIZE: ObjectSize = ObjectSize::from::<Self>();

    fn init(&mut self, slub: NonNull<Slub>) {
        *self = Self {
            slub: slub,
            partial_list: Spinlock::new(ListHead::empty()),
        };

        let mut partial_list = self.partial_list.lock();
        partial_list.init();
    }

    /// 创建一个Slub存放自身，作为 MemCacheNode 类型的 MemCache 的一个节点
    fn bootstrap() -> NonNull<Self> {
        let (_, object_num, order) = calculate_sizes(Self::OBJECT_SIZE, true);

        let mut slub =
            Slub::new(ZoneType::LinearMem, Self::OBJECT_SIZE.0, object_num, order).unwrap();

        unsafe {
            let _slub = slub.as_mut();
            let mut mem_cache_node: NonNull<Self> = match &mut _slub.slub_type {
                SlubType::Head(head) => head.allocate().unwrap(),
                _ => unreachable!(),
            };

            mem_cache_node.as_mut().init(slub);

            mem_cache_node
        }
    }

    /// 从已创建的 MemCacheNode 创建 "mem_cache_node" 的 MemCache
    pub fn bootstrap_cache(ptr: NonNull<Self>, mem_cache: &mut MemCache) -> NonNull<MemCache> {
        const NAME: *const u8 = b"mem_cache_node\0".as_ptr();

        mem_cache.new_boot_from_node(NAME, ptr, ObjectSize::from::<Self>())
    }

    // 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    pub fn new<T>(&mut self) -> Option<NonNull<MemCacheNode>> {
        let mut result = self.allocate::<MemCacheNode>()?;

        let (object_size, object_num, order) = calculate_sizes(ObjectSize::from::<T>(), true);

        unsafe {
            let slub = Slub::new(ZoneType::LinearMem, object_size, object_num, order).unwrap();

            result.as_mut().init(slub);
        }

        Some(result)
    }

    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        // 优先从正在使用的Slub分配
        let mut slub = self.slub;
        match &mut unsafe { slub.as_mut() }.slub_type {
            SlubType::Head(head) => head.allocate().or_else(|| {
                let mut result = None;
                let mut list_head = self.partial_list.lock();

                list_for_each_owner!(slub, SlubHead, list, list_head, {
                    result = unsafe { slub.as_mut().unwrap().allocate() };
                });

                result
            }),
            _ => unreachable!(),
        }
    }

    pub fn free<T>(&mut self, obj: NonNull<T>) -> Option<()> {
        // 先找到所在页的 Page 结构，再从其中取出 Slub 地址
        let page = unsafe {
            obj.map_addr(|addr| NonZero::new(page_align_down(addr.get())).unwrap())
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

/// 顶层 MemCache（非常简化）
pub struct MemCache {
    /// 链表节点，连接到 MemCaches 的 list_head
    pub list: ListNode<MemCache>,
    /// 名称（仅保存指针，字符串应位于只读数据段）
    pub name: *const u8,
    /// 对象大小（字节）
    pub object_size: NonZeroU16,
    /// 对象个数
    pub object_num: NonZeroU16,
    /// 页阶数
    pub order: PageOrder,
    /// 最小部分填充数量
    pub min_partial: u8,
    /// 对齐要求
    pub align: usize,
    /// 单一节点（忽略 NUMA）
    pub node: NonNull<MemCacheNode>,
    /// 全局自旋锁保护全局操作
    pub spinlock: SpinlockRaw,
}

unsafe impl Sync for MemCache {}

impl MemCache {
    #[inline]
    fn init(&mut self, name: *const u8, node: NonNull<MemCacheNode>, object_size: ObjectSize) {
        let (object_size, object_num, order) = calculate_sizes(object_size, true);

        let min_partial = (object_size.ilog2() as u8 / 2)
            .min(MAX_PARTIAL)
            .max(MIN_PARTIAL);

        *self = Self {
            list: ListNode::new(),
            name,
            object_size,
            object_num,
            order,
            align: ALIGN,
            min_partial,
            node,
            spinlock: SpinlockRaw::new_unlocked(),
        };
    }

    fn bootstrap(mut mem_cache_node: NonNull<MemCacheNode>) -> NonNull<Self> {
        const NAME: *const u8 = b"mem_cache\0".as_ptr();

        unsafe {
            // 申请 “mem_cache" 的 MemCacheNode
            let mut node = MemCacheNode::new::<MemCache>(mem_cache_node.as_mut()).unwrap();

            // 申请 "mem_cache" 的 MemCache
            let mut mem_cache = node.as_mut().allocate::<MemCache>().unwrap();

            mem_cache
                .as_mut()
                .init(NAME, node, ObjectSize::from::<Self>());

            mem_cache
        }
    }

    fn new_boot_from_node(
        &mut self,
        name: *const u8,
        mem_cache_node: NonNull<MemCacheNode>,
        object_size: ObjectSize,
    ) -> NonNull<Self> {
        // 申请 MemCache
        let node = unsafe { self.node.as_mut() };
        let mut mem_cache = node.allocate::<MemCache>().unwrap();

        unsafe {
            mem_cache.as_mut().init(name, mem_cache_node, object_size);
        };

        mem_cache
    }

    #[inline]
    fn new_boot(
        &mut self,
        name: *const u8,
        node_cache: &mut MemCache,
        object_size: ObjectSize,
    ) -> NonNull<Self> {
        let node_cache_node = unsafe { node_cache.node.as_mut() };
        let mem_cache_node = node_cache_node.new::<MemCache>().unwrap();

        self.new_boot_from_node(name, mem_cache_node, object_size)
    }

    /// 分配对象
    pub unsafe fn alloc(&mut self) -> *mut u8 {
        unimplemented!()
    }

    /// 释放对象
    pub unsafe fn free(&mut self, obj: *mut u8) {
        unimplemented!()
    }
}
