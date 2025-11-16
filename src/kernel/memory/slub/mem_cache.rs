use core::{
    cell::SyncUnsafeCell,
    mem::{replace, MaybeUninit},
    num::NonZeroU16,
    ptr::{null_mut, NonNull},
};

use crate::{
    kernel::memory::{
        buddy::PageOrder,
        page::ZoneType,
        slub::{calculate_sizes, mem_cache, Slub, ALIGN, MAX_PARTIAL, MIN_PARTIAL},
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::{Spinlock, SpinlockRaw},
    },
    list_for_each_owner,
};

pub const fn object_size<T: Sized>() -> core::num::NonZeroU16 {
    let size = size_of::<T>() as u16;
    assert!(size != 0, "size_of::<T>() == 0, NonZeroU16 required");
    // SAFETY: 已经断言 size != 0
    unsafe { core::num::NonZeroU16::new_unchecked(size.next_multiple_of(ALIGN as u16)) }
}

#[repr(C)]
pub struct MemCaches {
    pub list_head: Spinlock<ListHead<MemCache>>,
    pub mem_cache: NonNull<MemCache>,
    pub mem_cache_node: NonNull<MemCache>,
    pub slub_cache: NonNull<MemCache>,
}

unsafe impl Sync for MemCaches {}

impl MemCaches {
    pub fn init() {
        // 从零创建 MemCacheNode，对应 "mem_cache_node" 的 MemCacheNode
        let mem_cache_node = MemCacheNode::bootstrap();

        // 从mem_cache_node 创建 "mem_cache" 的 MemCacheNode，再从零创建 "mem_cache" 的 MemCache
        let mut mem_cache = MemCache::bootstrap(mem_cache_node);

        let mut mem_cache_node =
            unsafe { MemCacheNode::bootstrap_cache(mem_cache_node, mem_cache.as_mut()) };

        let slub_cache = unsafe {
            mem_cache
                .as_mut()
                .new_boot::<Slub>(b"slub_cache\0".as_ptr(), mem_cache_node.as_mut())
        };
        unsafe {
            let caches_uninit = &mut *CACHES.get();
            let caches = caches_uninit.write(MemCaches {
                list_head: Spinlock::new(ListHead::empty()),
                mem_cache,
                mem_cache_node,
                slub_cache,
            });

            let mem_cache_node = caches.mem_cache_node.as_mut();
            let mem_cache = caches.mem_cache.as_mut();
            let slub_cache = caches.slub_cache.as_mut();

            let mut list_head = caches.list_head.lock();
            list_head.init();
            list_head.add_head(&mut mem_cache_node.list);
            list_head.add_head(&mut mem_cache.list);
            list_head.add_head(&mut slub_cache.list);
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

/// 每个 CPU 的简单缓存（忽略 NUMA）
pub struct MemCacheCpu {
    // 简单的空闲链表头（按 u8 指针存放对象地址），真实实现需考虑对象布局/对齐
    freelist: *mut u8,
    partial_list: ListHead<Slub>,
    pub count: usize,
    // 使用自旋锁类型以保护 cpu cache
    spinlock: SpinlockRaw,
}

impl MemCacheCpu {
    pub const unsafe fn empty() -> Self {
        MemCacheCpu {
            freelist: null_mut(),
            partial_list: ListHead::empty(),
            count: 0,
            spinlock: SpinlockRaw::new_unlocked(),
        }
    }
}

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    pub partial_list: Spinlock<ListHead<Slub>>,
}

unsafe impl Sync for MemCacheNode {}

impl MemCacheNode {
    const OBJECT_SIZE: NonZeroU16 = mem_cache::object_size::<Slub>();

    fn init(&mut self, slub: &mut Slub) {
        debug_assert!(!slub.list.is_linked());
        unsafe {
            *self = Self {
                partial_list: Spinlock::new(ListHead::empty()),
            };

            {
                let mut partial_list = self.partial_list.lock();
                partial_list.init();
                partial_list.add_head(&mut slub.list);
            }
        };
    }

    /// 创建一个Slub存放自身，作为 MemCacheNode 类型的 MemCache 的一个节点
    fn bootstrap() -> NonNull<Self> {
        let (_, object_num, order) = calculate_sizes(Self::OBJECT_SIZE, true);

        let mut slub =
            Slub::new_embedded(ZoneType::MEM32, Self::OBJECT_SIZE, object_num, order).unwrap();

        unsafe {
            let _slub = slub.as_mut();
            let mut mem_cache_node: NonNull<Self> = _slub.allocate().unwrap();

            mem_cache_node.as_mut().init(_slub);

            mem_cache_node
        }
    }

    /// 从已创建的 MemCacheNode 创建 "mem_cache_node" 的 MemCache
    pub fn bootstrap_cache(ptr: NonNull<Self>, mem_cache: &mut MemCache) -> NonNull<MemCache> {
        const NAME: *const u8 = b"mem_cache_node\0".as_ptr();

        mem_cache.new_boot_from_node::<Self>(NAME, ptr)
    }

    // 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    pub fn new<T>(&mut self) -> Option<NonNull<MemCacheNode>> {
        let mut result = self.allocate::<MemCacheNode>()?;

        let (object_size, object_num, order) = calculate_sizes(object_size::<T>(), true);

        unsafe {
            let mut slub =
                Slub::new_embedded(ZoneType::MEM32, object_size, object_num, order).unwrap();

            result.as_mut().init(slub.as_mut());
        }

        Some(result)
    }

    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        let mut result = None;
        let mut list_head = self.partial_list.lock();

        list_for_each_owner!(slub, Slub, list, list_head, {
            result = unsafe { slub.as_mut().unwrap().allocate() };
        });

        result
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
    /// 每 CPU 缓存指针（为简化，这里仅提供单一 cpu cache）
    pub cpu: MemCacheCpu,
    /// 全局自旋锁保护全局操作
    pub spinlock: SpinlockRaw,
}

unsafe impl Sync for MemCache {}

impl MemCache {
    #[inline]
    fn init<T>(&mut self, name: *const u8, node: NonNull<MemCacheNode>) {
        let (object_size, object_num, order) = calculate_sizes(object_size::<T>(), true);

        let min_partial = (object_size.ilog2() as u8 / 2)
            .min(MAX_PARTIAL)
            .max(MIN_PARTIAL);

        unsafe {
            *self = Self {
                list: ListNode::new(),
                name,
                object_size,
                object_num,
                order,
                align: ALIGN,
                min_partial,
                node,
                cpu: MemCacheCpu::empty(),
                spinlock: SpinlockRaw::new_unlocked(),
            };
        }
    }

    fn bootstrap(mut mem_cache_node: NonNull<MemCacheNode>) -> NonNull<Self> {
        const NAME: *const u8 = b"mem_cache\0".as_ptr();

        unsafe {
            // 申请 “mem_cache" 的 MemCacheNode
            let mut node = MemCacheNode::new::<MemCache>(mem_cache_node.as_mut()).unwrap();

            // 申请 "mem_cache" 的 MemCache
            let mut mem_cache = node.as_mut().allocate::<MemCache>().unwrap();

            mem_cache.as_mut().init::<Self>(NAME, node);

            mem_cache
        }
    }

    fn new_boot_from_node<T>(
        &mut self,
        name: *const u8,
        mem_cache_node: NonNull<MemCacheNode>,
    ) -> NonNull<Self> {
        // 申请 MemCache
        let node = unsafe { self.node.as_mut() };
        let mut mem_cache = node.allocate::<MemCache>().unwrap();

        unsafe {
            mem_cache.as_mut().init::<T>(name, mem_cache_node);
        };

        mem_cache
    }

    #[inline]
    fn new_boot<T>(&mut self, name: *const u8, node_cache: &mut MemCache) -> NonNull<Self> {
        let node_cache_node = unsafe { node_cache.node.as_mut() };
        let mem_cache_node = node_cache_node.new::<MemCache>().unwrap();

        self.new_boot_from_node::<T>(name, mem_cache_node)
    }

    /// 分配对象（stub）：尝试从 CPU 缓存取出，否则返回 null
    /// 真实实现应尝试 refill 或从 node/global 获取对象
    pub unsafe fn alloc(&mut self) -> *mut u8 {
        // 尝试从 cpu freelist 获取（简化：不做复杂的原子操作，只用自旋锁保护）
        self.cpu.spinlock.lock();
        let obj = if !self.cpu.freelist.is_null() {
            // 把 freelist 当单链表：头部存储下一个指针
            let next = *(self.cpu.freelist as *mut *mut u8);
            let ret = self.cpu.freelist;
            self.cpu.freelist = next;
            self.cpu.count = self.cpu.count.saturating_sub(1);
            ret
        } else {
            null_mut()
        };
        self.cpu.spinlock.unlock();
        obj
    }

    /// 释放对象到 CPU 缓存（stub）
    pub unsafe fn free(&mut self, obj: *mut u8) {
        if obj.is_null() {
            return;
        }
        self.cpu.spinlock.lock();
        // 将 obj 的起始地址写为当前 freelist 头（单链表），并更新头
        let head_ptr = &mut self.cpu.freelist as *mut *mut u8;
        // 在内核中需要确保写入安全（对象大小足够存放指针）
        *(obj as *mut *mut u8) = self.cpu.freelist;
        self.cpu.freelist = obj;
        self.cpu.count = self.cpu.count.saturating_add(1);
        self.cpu.spinlock.unlock();
    }
}
