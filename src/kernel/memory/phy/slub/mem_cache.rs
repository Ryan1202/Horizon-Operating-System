use core::{
    cell::SyncUnsafeCell,
    mem::{MaybeUninit, offset_of},
    num::NonZeroU16,
    pin::Pin,
    ptr::{NonNull, null_mut},
    sync::atomic::{AtomicPtr, Ordering},
};

use crate::{
    kernel::memory::phy::{
        frame::{Frame, options::FrameAllocOptions},
        slub::{
            ALIGN, MAX_PARTIAL, MIN_PARTIAL, Slub, SlubError, calculate_sizes,
            config::{DEFAULT_CACHE_CONFIGS, DEFAULT_CACHES},
            mem_cache_node::MemCacheNode,
        },
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::{Spinlock, SpinlockRaw},
    },
};

#[repr(C)]
pub struct MemCaches {
    pub list_head: Spinlock<ListHead<MemCache>>,
    pub mem_cache: NonNull<MemCache>,
    pub mem_cache_node: NonNull<MemCache>,
}

unsafe impl Sync for MemCaches {}

impl MemCaches {
    pub fn init() {
        let mut frame_options = DEFAULT_FRAME_OPTIONS;

        // 从零创建 MemCacheNode，对应 "mem_cache_node" 的 MemCacheNode
        let mem_cache_node = MemCacheNode::bootstrap(&mut frame_options);

        // 从 mem_cache_node 创建 "mem_cache" 的 MemCacheNode，再从零创建 "mem_cache" 的 MemCache
        let mut mem_cache = MemCache::bootstrap(mem_cache_node, frame_options);

        // 将 mem_cache_node 放入 "mem_cache"
        let mem_cache_node = unsafe {
            MemCacheNode::bootstrap_cache(mem_cache_node, mem_cache.as_mut(), frame_options)
        };

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
                    cache.object_size,
                    frame_options,
                );
            }

            caches.list_head.init_with(|v| {
                let mut head = Pin::new_unchecked(v);
                head.init();

                head.add_head(Pin::new_unchecked(&mut mem_cache_node.list));
                head.add_head(Pin::new_unchecked(&mut mem_cache.list));

                for mut cache in DEFAULT_CACHES {
                    head.add_tail(Pin::new_unchecked(&mut cache.as_mut().list));
                }
            });
        }
    }

    pub fn add_cache(&mut self, cache: Pin<&mut MemCache>) {
        let mut list_head = self.list_head.lock();
        unsafe {
            Pin::new_unchecked(&mut *list_head).add_head(cache.map_unchecked_mut(|v| &mut v.list))
        };
    }
}

static CACHES: SyncUnsafeCell<MaybeUninit<MemCaches>> = SyncUnsafeCell::new(MaybeUninit::uninit());

/// 顶层 MemCache（非常简化）
pub struct MemCache {
    /// 链表节点，连接到 MemCaches 的 list_head
    list: ListNode<MemCache>,
    /// 名称（仅保存指针，字符串应位于只读数据段）
    name: *const u8,
    /// 指向 Slub 的原子指针
    slub: AtomicPtr<Frame>,
    /// 原始对象大小（字节）
    original_size: NonZeroU16,
    /// 对象个数
    object_num: NonZeroU16,
    /// 最小部分填充数量
    min_partial: u8,
    /// 对齐要求
    align: usize,
    /// 单一节点（忽略 NUMA）
    node: NonNull<MemCacheNode>,
    /// 全局自旋锁保护全局操作
    spinlock: SpinlockRaw,
    /// 物理页分配选项
    pub frame_options: FrameAllocOptions,
}

unsafe impl Sync for MemCache {}

pub const DEFAULT_FRAME_OPTIONS: FrameAllocOptions = FrameAllocOptions::new();

impl MemCache {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(64).unwrap();

    #[inline]
    fn init(
        &mut self,
        name: *const u8,
        mut node: NonNull<MemCacheNode>,
        size: NonZeroU16,
        frame_options: FrameAllocOptions,
    ) -> Result<(), SlubError> {
        let (object_size, object_num, order) = calculate_sizes(size)?;

        let min_partial = (object_size.0.ilog2() as u8 / 2)
            .min(MAX_PARTIAL)
            .max(MIN_PARTIAL);

        let frame = {
            let mut partial_list = unsafe { node.as_mut() }.partial_list.lock();

            // 从 MemCacheNode 的 partial list 中获取一个 Slub 作为首选 Slub
            let frame = partial_list
                .iter(offset_of!(Frame, list))
                .next()
                .expect("Trying to init Memcache from a node without slub!");
            unsafe {
                Pin::new_unchecked(frame.list.get_mut()).del();
            }
            frame
        };

        *self = Self {
            list: ListNode::new(),
            name,
            slub: AtomicPtr::new(frame as *mut Frame),
            original_size: size,
            object_num,
            align: ALIGN,
            min_partial,
            node,
            spinlock: SpinlockRaw::new_unlocked(),
            frame_options: frame_options.dynamic(order),
        };

        Ok(())
    }

    fn bootstrap(
        mut mem_cache_node: NonNull<MemCacheNode>,
        frame_options: FrameAllocOptions,
    ) -> NonNull<Self> {
        const NAME: *const u8 = b"mem_cache\0".as_ptr();

        unsafe {
            // 申请 "mem_cache" 的 MemCacheNode
            let mut node = mem_cache_node
                .as_mut()
                .new(Self::OBJECT_SIZE, frame_options)
                .unwrap();

            // 申请 "mem_cache" 的 MemCache
            let mut mem_cache = node.as_mut().allocate::<MemCache>(frame_options).unwrap();

            mem_cache
                .as_mut()
                .init(NAME, node, Self::OBJECT_SIZE, frame_options)
                .expect("Unexpected error ocurred when bootstrapping MemCache!");

            mem_cache
        }
    }

    pub(super) fn new_boot_from_node(
        &mut self,
        name: *const u8,
        mem_cache_node: NonNull<MemCacheNode>,
        object_size: NonZeroU16,
        frame_options: FrameAllocOptions,
    ) -> NonNull<Self> {
        // 申请 MemCache
        let node = unsafe { self.node.as_mut() };
        let mut mem_cache = node
            .allocate::<MemCache>(frame_options)
            .expect("Unexpected error ocurred when allocating bootstrap MemCache from node!");

        unsafe {
            mem_cache
                .as_mut()
                .init(name, mem_cache_node, object_size, frame_options)
                .expect("Unexpected error ocurred when init bootstrap MemCache from node!");
        };

        mem_cache
    }

    #[inline]
    fn new_boot(
        &mut self,
        name: *const u8,
        node_cache: &mut MemCache,
        object_size: NonZeroU16,
        frame_options: FrameAllocOptions,
    ) -> NonNull<Self> {
        let node_cache_node = unsafe { node_cache.node.as_mut() };
        let mem_cache_node = node_cache_node
            .new(object_size, frame_options)
            .expect("Unexpected error ocurred when creating bootstrap MemCache node!");

        self.new_boot_from_node(name, mem_cache_node, object_size, frame_options)
    }

    /// 分配对象
    /// 返回`Option<NonNull<T>>`，指向类型为`T`的已分配对象
    ///
    /// 以下情况下会返回`None`：
    /// - `Node`中没有可用的对象，且尝试创建新`Slub`失败
    pub fn alloc<T>(&mut self) -> Option<NonNull<T>> {
        // 交换出 Slub 指针并用空指针替代，防止并发分配冲突
        let slub_ptr = self.slub.swap(null_mut(), Ordering::AcqRel);

        if !slub_ptr.is_null() {
            // 从 Slub 中分配
            let mut frame = unsafe { &mut *slub_ptr };
            let slub = Slub::from_frame(&mut frame);
            let result = match slub {
                Slub::Head(head) => head.allocate::<T>(),
                _ => {
                    unreachable!("MemCache's Slub should always be SlubHead!");
                }
            };

            // 重新存回 Slub 指针
            self.slub.store(slub_ptr, Ordering::Release);
            if let Some(result) = result {
                return Some(result);
            }
        }

        // 未初始化或者已被占用，从 Node 中分配
        let node = unsafe { self.node.as_mut() };
        Some(node.allocate::<T>(self.frame_options).unwrap())
    }
}
