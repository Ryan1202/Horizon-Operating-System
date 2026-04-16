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
            Slub, SlubError,
            config::{
                CacheConfig, DEFAULT_CACHE_CONFIGS, DEFAULT_CACHE_COUNT, DEFAULT_CACHES,
                get_cache_unchecked,
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
    pub mem_cache: &'static MemCache,
    pub node_mem_cache: &'static MemCache,
}

impl MemCaches {
    pub fn init() {
        const OPTIONS: PageAllocOptions =
            PageAllocOptions::new(FrameAllocOptions::new()).contiguous(true);

        // 从零创建 MemCacheNode，对应 "mem_cache_node" 的 MemCacheNode
        let mem_cache_node = MemCacheNode::bootstrap(OPTIONS);

        // 从 mem_cache_node 创建 "mem_cache" 的 MemCacheNode，再从零创建 "mem_cache" 的 MemCache
        let mut mem_cache = MemCache::bootstrap(unsafe { mem_cache_node.as_ref() }, OPTIONS);

        // 将 mem_cache_node 放入 "mem_cache"
        let mut mem_cache_node = unsafe {
            MemCacheNode::bootstrap_cache(mem_cache_node, mem_cache.as_ref(), OPTIONS)
                .expect("Unexpected error occurred when bootstrapping MemCache")
        };

        unsafe {
            let caches_uninit = &mut *CACHES.get();
            let caches = caches_uninit.write(MemCaches {
                list_head: Spinlock::new(ListHead::empty()),
                mem_cache: mem_cache.as_ref(),
                node_mem_cache: mem_cache_node.as_ref(),
            });

            {
                let mem_cache_node = caches.node_mem_cache;
                let mem_cache = caches.mem_cache;

                for (i, cache) in DEFAULT_CACHE_CONFIGS.iter().enumerate() {
                    let cache_ptr = &mut *DEFAULT_CACHES.get();
                    cache_ptr[i].store(
                        MemCache::new_raw(*cache, mem_cache_node, mem_cache, OPTIONS)
                            .expect("Unexpected error occurred when creating kmalloc- MemCache")
                            .as_ptr(),
                        Ordering::Release,
                    );
                }
            }

            caches.list_head.init_with(|v| {
                let mut head = Pin::new_unchecked(v);
                head.init();

                head.add_head(mem_cache_node.as_mut().get_list());
                head.add_head(mem_cache.as_mut().get_list());

                for cache in 0..DEFAULT_CACHE_COUNT {
                    let mut cache = get_cache_unchecked(cache);
                    head.add_tail(cache.as_mut().get_list());
                }
            });
        }
    }

    pub fn add_cache(&self, cache: Pin<&mut MemCache>) {
        let mut list_head = self.list_head.lock();
        unsafe {
            Pin::new_unchecked(&mut *list_head)
                .add_head(cache.map_unchecked_mut(|v| &mut *v.list.get()))
        };
    }
}

static CACHES: SyncUnsafeCell<MaybeUninit<MemCaches>> = SyncUnsafeCell::new(MaybeUninit::uninit());

/// 顶层 MemCache
pub struct MemCache {
    /// 链表节点，连接到 MemCaches 的 list_head
    list: SyncUnsafeCell<ListNode<MemCache>>,
    /// 配置
    pub(super) config: CacheConfig,
    /// 指向 Slub 的原子指针
    slub: AtomicPtr<Slub>,
    /// 单一节点（忽略 NUMA）
    node: NonNull<MemCacheNode>,
    /// 页分配选项
    pub options: PageAllocOptions,
}

unsafe impl Sync for MemCache {}

impl MemCache {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(size_of::<Self>() as u16).unwrap();
    const CONFIG: CacheConfig = CacheConfig::new(c"MemCache", Self::OBJECT_SIZE)
        .ok()
        .unwrap();

    #[inline]
    pub(super) fn init(
        &mut self,
        config: CacheConfig,
        node: NonNull<MemCacheNode>,
        mut options: PageAllocOptions,
    ) -> Result<(), SlubError> {
        options = options.order(config.frame_order);

        *self = Self {
            list: SyncUnsafeCell::new(ListNode::new()),
            config,
            slub: AtomicPtr::null(),
            node,
            options,
        };

        Ok(())
    }

    fn bootstrap(mem_cache_node: &MemCacheNode, mut options: PageAllocOptions) -> NonNull<Self> {
        unsafe {
            let config = &Self::CONFIG;

            // 申请 "mem_cache" 的 MemCacheNode
            let mut node = mem_cache_node.new_self(config, options).unwrap();
            options = options.order(config.frame_order);

            let slub = Slub::new(&Self::CONFIG, options).unwrap();
            node.as_mut().init(config, Some(slub));

            // 申请 "mem_cache" 的 MemCache
            let mut mem_cache = {
                let node = node.as_ref();
                let slub = node.get(config, options).unwrap();
                let result =
                    slub.as_ref()
                        .allocate::<MemCache>(NonNull::dangling())
                        .map(|mem_cache| {
                            node.put(config, options, slub);
                            mem_cache
                        });
                result.unwrap()
            };

            mem_cache
                .as_mut()
                .init(Self::CONFIG, node, options)
                .expect("Unexpected error ocurred when bootstrapping MemCache!");

            mem_cache
        }
    }

    fn get_list(&mut self) -> Pin<&mut ListNode<MemCache>> {
        unsafe { Pin::new_unchecked(self.list.get_mut()) }
    }

    /// 不依赖全局变量的实现，可以在初始化时简化调用
    ///
    /// 默认初始化时不预分配 Slub
    #[inline]
    fn new_raw(
        config: CacheConfig,
        node_cache: &MemCache,
        mem_cache: &MemCache,
        options: PageAllocOptions,
    ) -> Option<NonNull<Self>> {
        let mut node = node_cache.allocate::<MemCacheNode>()?;

        mem_cache
            .allocate::<MemCache>()
            .and_then(|mut ptr| unsafe {
                let mem_cache = ptr.as_mut();

                node.as_mut().init(&config, None);
                mem_cache
                    .init(config, node, options.order(config.frame_order))
                    .ok()?;

                Some(ptr)
            })
            .or_else(|| {
                let _ = kfree(node).inspect_err(|e| {
                    printk!("kfree failed when failed to create MemCache: {:?}", e);
                });
                None
            })
    }

    pub fn new(config: CacheConfig, options: PageAllocOptions) -> Option<NonNull<Self>> {
        let caches = unsafe { (*CACHES.get()).assume_init_ref() };

        unsafe {
            Self::new_raw(config, caches.node_mem_cache, caches.mem_cache, options).and_then(
                |mut mem_cache| {
                    caches.add_cache(Pin::new_unchecked(mem_cache.as_mut()));
                    Some(mem_cache)
                },
            )
        }
    }

    /// 将 Slub 保存
    ///
    /// 如果 self.slub 为空则写入，否则加入 MemCacheNode 中
    fn store(&self, slub: &mut Slub) {
        if slub.inner.get_relaxed().inuse == self.config.object_num.get() {
            // 如果满了就丢弃
            return;
        }
        if let Err(slub) =
            self.slub
                .compare_exchange(null_mut(), slub, Ordering::Release, Ordering::Relaxed)
        {
            // 被抢先写入了，放进 node
            unsafe {
                self.node
                    .as_ref()
                    .put(&self.config, self.options, NonNull::new_unchecked(slub))
            };
        }
    }

    /// 分配对象
    /// 返回`Option<NonNull<T>>`，指向类型为`T`的已分配对象
    ///
    /// 以下情况下会返回`None`：
    /// - `Node`中没有可用的对象，且尝试创建新`Slub`失败
    pub fn allocate<T>(&self) -> Option<NonNull<T>> {
        // 交换出 Slub 指针并用空指针替代，防止并发分配冲突
        let slub_ptr = self.slub.swap(null_mut(), Ordering::AcqRel);

        if !slub_ptr.is_null() {
            // 从 Slub 中分配
            let slub = unsafe { &mut *slub_ptr };
            let result = slub.allocate(NonNull::from_ref(self));

            // 重新存回 Slub 指针
            self.store(slub);

            if let Some(ptr) = result {
                return Some(ptr);
            }
        }

        // 未初始化或者已被占用，从 Node 中分配
        unsafe {
            let node = self.node.as_ref();
            let slub = node.get(&self.config, self.options)?.as_mut();
            let result = slub.allocate(NonNull::from_ref(self));

            self.store(slub);

            result
        }
    }

    pub fn try_destory(mut ptr: NonNull<Self>) -> Option<()> {
        let mem_cache = unsafe { ptr.as_mut() };
        unsafe { mem_cache.node.as_mut().try_destroy(&mem_cache.options)? };

        // 交换出 Slub 指针并用空指针替代
        let slub_ptr = mem_cache.slub.swap(null_mut(), Ordering::Relaxed);

        if !slub_ptr.is_null() {
            // 销毁 Slub
            let frame = unsafe { &mut *slub_ptr }.try_into().ok();
            frame
                .and_then(|slub: &mut Slub| slub.try_destroy(&mem_cache.options))
                .or_else(|| {
                    mem_cache.slub.store(slub_ptr, Ordering::Relaxed);
                    None
                })?;
        }

        let mut head = unsafe { (*CACHES.get()).assume_init_ref().list_head.lock() };

        let list = mem_cache.get_list();
        unsafe { Pin::new_unchecked(&mut *head) }.del(list);

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
