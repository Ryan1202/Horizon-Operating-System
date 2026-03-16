use core::{
    cell::SyncUnsafeCell,
    ffi::{CStr, c_char, c_void},
    mem::{MaybeUninit, transmute},
    num::NonZeroU16,
    pin::Pin,
    ptr::{NonNull, null_mut},
    sync::atomic::{AtomicPtr, Ordering},
};

use crate::{
    kernel::memory::{
        frame::options::FrameAllocOptions,
        kmalloc::kfree,
        page::options::PageAllocOptions,
        slub::{
            MAX_PARTIAL, MIN_PARTIAL, Slub, SlubError,
            config::{
                CacheConfig, DEFAULT_CACHE_CONFIGS, DEFAULT_CACHE_COUNT, DEFAULT_CACHES, get_cache,
            },
            mem_cache_node::MemCacheNode,
        },
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
};

#[repr(C)]
pub struct MemCaches {
    pub list_head: Spinlock<ListHead<MemCache>>,
    pub mem_cache: NonNull<MemCache>,
    pub node_mem_cache: NonNull<MemCache>,
}

unsafe impl Sync for MemCaches {}

impl MemCaches {
    pub fn init() {
        const OPTIONS: PageAllocOptions =
            PageAllocOptions::new(FrameAllocOptions::new()).contiguous(true);

        // 从零创建 MemCacheNode，对应 "mem_cache_node" 的 MemCacheNode
        let mem_cache_node = MemCacheNode::bootstrap(OPTIONS);

        // 从 mem_cache_node 创建 "mem_cache" 的 MemCacheNode，再从零创建 "mem_cache" 的 MemCache
        let mut mem_cache = MemCache::bootstrap(mem_cache_node, OPTIONS);

        // 将 mem_cache_node 放入 "mem_cache"
        let mem_cache_node = unsafe {
            MemCacheNode::bootstrap_cache(mem_cache_node, mem_cache.as_mut(), OPTIONS)
                .expect("Unexpected error occurred when bootstrapping MemCache")
        };

        unsafe {
            let caches_uninit = &mut *CACHES.get();
            let caches = caches_uninit.write(MemCaches {
                list_head: Spinlock::new(ListHead::empty()),
                mem_cache,
                node_mem_cache: mem_cache_node,
            });

            let mem_cache_node = caches.node_mem_cache.as_mut();
            let mem_cache = caches.mem_cache.as_mut();

            for (i, cache) in DEFAULT_CACHE_CONFIGS.iter().enumerate() {
                let cache_ptr = &mut *DEFAULT_CACHES.get();
                cache_ptr[i].store(
                    mem_cache
                        .new_boot(*cache, mem_cache_node, OPTIONS)
                        .expect("Unexpected error occurred when creating kmalloc- MemCache")
                        .as_ptr(),
                    Ordering::Release,
                );
            }

            caches.list_head.init_with(|v| {
                let mut head = Pin::new_unchecked(v);
                head.init();

                head.add_head(Pin::new_unchecked(&mut mem_cache_node.list));
                head.add_head(Pin::new_unchecked(&mut mem_cache.list));

                for cache in 0..DEFAULT_CACHE_COUNT {
                    head.add_tail(Pin::new_unchecked(
                        &mut get_cache(cache).unwrap().as_mut().list,
                    ));
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

/// 顶层 MemCache
pub struct MemCache {
    /// 链表节点，连接到 MemCaches 的 list_head
    list: ListNode<MemCache>,
    /// 配置
    pub config: CacheConfig,
    /// 指向 Slub 的原子指针
    slub: AtomicPtr<Slub>,
    /// 最小部分填充数量
    _min_partial: u8,
    /// 单一节点（忽略 NUMA）
    node: NonNull<MemCacheNode>,
    /// 页分配选项
    pub options: PageAllocOptions,
}

impl MemCache {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(size_of::<Self>() as u16).unwrap();
    const CONFIG: CacheConfig = CacheConfig::new(c"MemCache", Self::OBJECT_SIZE)
        .ok()
        .unwrap();

    #[inline]
    fn init(
        &mut self,
        config: CacheConfig,
        mut node: NonNull<MemCacheNode>,
        mut options: PageAllocOptions,
    ) -> Result<(), SlubError> {
        options = options.order(config.frame_order);

        let min_partial = (config.object_size.0.ilog2() as u8 / 2).clamp(MIN_PARTIAL, MAX_PARTIAL);

        let slub = {
            let mut partial_list = unsafe { node.as_mut() }.partial_list.lock();

            // 从 MemCacheNode 的 partial list 中获取一个 Slub 作为首选 Slub
            let slub = unsafe {
                partial_list
                    .iter(Slub::list_offset())
                    .next()
                    .expect("Trying to init Memcache from a node without slub!")
                    .as_mut()
            };
            slub.get_list().del(&mut partial_list);
            slub
        };

        *self = Self {
            list: ListNode::new(),
            config,
            slub: AtomicPtr::new(slub as *mut Slub),
            _min_partial: min_partial,
            node,
            options,
        };

        Ok(())
    }

    fn bootstrap(
        mut mem_cache_node: NonNull<MemCacheNode>,
        mut options: PageAllocOptions,
    ) -> NonNull<Self> {
        unsafe {
            // 申请 "mem_cache" 的 MemCacheNode
            let mut node = mem_cache_node
                .as_mut()
                .new_self(&Self::CONFIG, options)
                .unwrap();
            options = options.order(Self::CONFIG.frame_order);

            let slub = Slub::new(&Self::CONFIG, options).unwrap();
            node.as_mut().init(&Self::CONFIG, Some(slub));

            // 申请 "mem_cache" 的 MemCache
            let mut mem_cache = node
                .as_mut()
                .allocate::<MemCache>(&Self::CONFIG, options)
                .unwrap();

            mem_cache
                .as_mut()
                .init(Self::CONFIG, node, options)
                .expect("Unexpected error ocurred when bootstrapping MemCache!");

            mem_cache
        }
    }

    pub(super) fn new_boot_from_node(
        &mut self,
        config: CacheConfig,
        mem_cache_node: NonNull<MemCacheNode>,
        options: PageAllocOptions,
    ) -> Option<NonNull<Self>> {
        // 申请 MemCache
        let node = unsafe { self.node.as_mut() };
        let mut mem_cache = node.allocate::<MemCache>(&self.config, self.options)?;

        unsafe {
            mem_cache
                .as_mut()
                .init(config, mem_cache_node, options)
                .ok()?;
        };

        Some(mem_cache)
    }

    #[inline]
    fn new_boot(
        &mut self,
        config: CacheConfig,
        node_cache: &mut MemCache,
        mut options: PageAllocOptions,
    ) -> Option<NonNull<Self>> {
        let node_cache_node = unsafe { node_cache.node.as_mut() };

        options = options.order(config.frame_order);

        let mut mem_cache_node =
            node_cache_node.new_self(&node_cache.config, node_cache.options)?;
        unsafe {
            let slub = Slub::new(&config, options).ok();
            mem_cache_node.as_mut().init(&config, slub);
        }

        self.new_boot_from_node(config, mem_cache_node, options)
    }

    pub fn new(config: CacheConfig, options: PageAllocOptions) -> Option<NonNull<Self>> {
        let caches = unsafe { (&mut *CACHES.get()).assume_init_mut() };

        let mem_cache_node = MemCacheNode::new(unsafe { caches.node_mem_cache.as_mut() }, options)?;

        unsafe {
            let result = caches.mem_cache.as_mut().allocate::<Self>();

            if let Some(mut mem_cache_ptr) = result {
                let mem_cache = mem_cache_ptr.as_mut();

                mem_cache.init(config, mem_cache_node, options).ok()?;

                caches.add_cache(Pin::new_unchecked(mem_cache));

                result
            } else {
                let _ = kfree(mem_cache_node).inspect_err(|e| {
                    printk!("kfree failed when failed to create MemCache: {:?}", e);
                });
                None
            }
        }
    }

    /// 分配对象
    /// 返回`Option<NonNull<T>>`，指向类型为`T`的已分配对象
    ///
    /// 以下情况下会返回`None`：
    /// - `Node`中没有可用的对象，且尝试创建新`Slub`失败
    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        // 交换出 Slub 指针并用空指针替代，防止并发分配冲突
        let slub_ptr = self.slub.swap(null_mut(), Ordering::AcqRel);

        if !slub_ptr.is_null() {
            // 从 Slub 中分配
            let slub = unsafe { &mut *slub_ptr };
            let result = slub.allocate();

            // 重新存回 Slub 指针
            self.slub.store(slub_ptr, Ordering::Release);
            if let Some(result) = result {
                return Some(result);
            }
        }

        // 未初始化或者已被占用，从 Node 中分配
        let node = unsafe { self.node.as_mut() };
        Some(node.allocate::<T>(&self.config, self.options).unwrap())
    }

    pub fn try_destory(mut ptr: NonNull<Self>) -> Option<()> {
        let mem_cache = unsafe { ptr.as_mut() };
        unsafe { mem_cache.node.as_mut().try_destroy(mem_cache)? };

        // 交换出 Slub 指针并用空指针替代
        let slub_ptr = mem_cache.slub.swap(null_mut(), Ordering::Relaxed);

        if !slub_ptr.is_null() {
            // 销毁 Slub
            let frame = unsafe { &mut *slub_ptr };
            frame
                .try_into()
                .ok()
                .and_then(|slub: &mut Slub| slub.try_destroy(mem_cache))
                .or_else(|| {
                    mem_cache.slub.store(slub_ptr, Ordering::Relaxed);
                    None
                })?;
        }

        let mut head = unsafe { &mut (*CACHES.get()).assume_init_mut().list_head }.lock();
        let list = &mut mem_cache.list;
        unsafe { Pin::new_unchecked(list).del(&mut head) };

        let _ = kfree(mem_cache.node).inspect_err(|e| printk!("Free MemCacheNode failed: {:?}", e));
        let _ = kfree(ptr).inspect_err(|e| printk!("Free MemCache failed: {:?}", e));

        Some(())
    }
}

#[unsafe(export_name = "mem_cache_create")]
pub extern "C" fn mem_cache_create_c(
    name: *const c_char,
    object_size: u16,
    align: usize,
) -> *mut MemCache {
    let object_size = match NonZeroU16::new(object_size) {
        Some(size) => size,
        None => return null_mut(),
    };

    let config = match unsafe {
        CacheConfig::new(CStr::from_ptr(name), object_size).map(|config| config.align(align))
    } {
        Ok(config) => config,
        Err(_) => return null_mut(),
    };

    unsafe {
        transmute(MemCache::new(
            config,
            PageAllocOptions::new(FrameAllocOptions::new()),
        ))
    }
}

#[unsafe(export_name = "mem_cache_destroy")]
pub extern "C" fn mem_cache_destroy_c(ptr: *mut MemCache) -> i32 {
    if let Some(ptr) = NonNull::new(ptr) {
        if MemCache::try_destory(ptr).is_none() {
            printk!("Failed to destroy MemCache at {:p}", ptr);
            -1
        } else {
            0
        }
    } else {
        -1
    }
}

#[unsafe(export_name = "mem_cache_alloc")]
pub extern "C" fn mem_cache_alloc_c(ptr: *mut MemCache) -> *mut c_void {
    if let Some(mut ptr) = NonNull::new(ptr) {
        let mem_cache = unsafe { ptr.as_mut() };
        mem_cache
            .allocate()
            .map(|p| p.as_ptr())
            .unwrap_or(null_mut())
    } else {
        null_mut()
    }
}
