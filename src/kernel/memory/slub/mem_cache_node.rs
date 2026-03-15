use core::{num::NonZeroU16, ops::DerefMut, pin::Pin, ptr::NonNull};

use crate::{
    kernel::memory::{
        page::options::PageAllocOptions,
        slub::{ObjectSize, Slub, config::CacheConfig, mem_cache::MemCache},
    },
    lib::rust::{list::ListHead, spinlock::Spinlock},
};

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    pub partial_list: Spinlock<ListHead<Slub>>,
    object_size: ObjectSize,
}

impl MemCacheNode {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(size_of::<Self>() as u16).unwrap();
    const CONFIG: CacheConfig = CacheConfig::new(c"MemCacheNode", Self::OBJECT_SIZE)
        .ok()
        .unwrap();

    pub fn init(&mut self, config: &CacheConfig, slub: Option<NonNull<Slub>>) {
        *self = Self {
            partial_list: Spinlock::new(ListHead::empty()),
            object_size: config.object_size,
        };

        unsafe {
            self.partial_list.init_with(|v| {
                let mut head = Pin::new_unchecked(v);
                head.as_mut().init();

                if let Some(mut slub) = slub {
                    let slub = slub.as_mut().get_list();
                    head.add_tail(slub);
                }
            })
        };
    }

    /// 创建一个Slub存放自身，作为 MemCacheNode 类型的 MemCache 的一个节点
    pub(super) fn bootstrap(mut options: PageAllocOptions) -> NonNull<Self> {
        let config = &Self::CONFIG;

        options.frame = options.frame.dynamic(config.frame_order);

        let mut slub = Slub::new(config, options).unwrap();

        unsafe {
            let _slub = slub.as_mut();
            let mut mem_cache_node: NonNull<Self> = _slub.allocate().unwrap();

            mem_cache_node.as_mut().init(config, Some(slub));

            mem_cache_node
        }
    }

    /// 从已创建的 MemCacheNode 创建 "mem_cache_node" 的 MemCache
    pub fn bootstrap_cache(
        mem_cache_node: NonNull<Self>,
        mem_cache: &mut MemCache,
        options: PageAllocOptions,
    ) -> Option<NonNull<MemCache>> {
        mem_cache.new_boot_from_node(Self::CONFIG, mem_cache_node, options)
    }

    // 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    pub fn new_self(
        &mut self,
        config: &CacheConfig,
        options: PageAllocOptions,
    ) -> Option<NonNull<Self>> {
        assert!(self.object_size.0 == Self::CONFIG.object_size.0);

        self.allocate(config, options)
    }

    pub fn new(node_cache: &mut MemCache, mut options: PageAllocOptions) -> Option<NonNull<Self>> {
        assert!(node_cache.config.origin_size == Self::CONFIG.origin_size);

        let config = &Self::CONFIG;

        options.frame = options.frame.dynamic(config.frame_order);

        let mut result = node_cache.allocate::<MemCacheNode>()?;

        unsafe {
            let slub = Slub::new(config, options).ok()?;

            result.as_mut().init(config, Some(slub));
        }

        Some(result)
    }

    /// 从`Node`中分配对象
    ///
    /// 默认从`partial_list`中分配对象，若未分配到则尝试创建一个新的`Slub`并从中分配
    ///
    /// 新的`Slub`分配失败时返回`None`
    pub fn allocate<T>(
        &mut self,
        config: &CacheConfig,
        options: PageAllocOptions,
    ) -> Option<NonNull<T>> {
        if !self.partial_list.get_relaxed().is_empty() {
            let mut partial_list = self.partial_list.lock();

            for mut slub in partial_list.iter(Slub::list_offset()) {
                match unsafe { slub.as_mut().allocate() } {
                    Some(ptr) => {
                        return Some(ptr);
                    }
                    None => {}
                }
            }
        }

        // 从partial_list中没有分配到，创建一个新的Slub
        let mut new_slub = Slub::new(config, options).ok()?;

        let new_slub = unsafe { new_slub.as_mut() };
        {
            let mut head = self.partial_list.lock();

            let list = new_slub.get_list();

            unsafe { Pin::new_unchecked(head.deref_mut()) }.add_head(list);

            new_slub.allocate()
        }
    }

    pub fn try_destroy(&mut self, cache_info: &MemCache) -> Option<()> {
        let mut list = self.partial_list.lock();

        for mut slub in list.iter(Slub::list_offset()) {
            let slub = unsafe { slub.as_mut() };
            {
                let mut head = self.partial_list.lock();
                slub.get_list().del(head.deref_mut());
            }

            slub.try_destroy(cache_info)?;
        }

        Some(())
    }
}
