use core::{num::NonZeroU16, pin::Pin, ptr::NonNull};

use crate::{
    kernel::memory::{
        page::options::PageAllocOptions,
        slub::{ObjectSize, Slub, config::CacheConfig, mem_cache::MemCache},
    },
    lib::rust::{list::ListHead, spinlock::Spinlock},
};

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    partial_list: Spinlock<PartialList>,
    object_size: ObjectSize,
}

struct PartialList {
    list_head: ListHead<Slub>,
    count: usize,
}

impl Spinlock<PartialList> {
    fn get(&self, config: &CacheConfig, options: PageAllocOptions) -> Option<NonNull<Slub>> {
        (if !self.get_relaxed().list_head.is_empty() {
            let mut guard = self.lock();

            guard
                .list_head
                .iter(Slub::list_offset())
                .next()
                .and_then(|mut slub| unsafe {
                    slub.as_mut().get_list().del(&mut guard.list_head);
                    guard.count -= 1;
                    Some(slub)
                })
        } else {
            None
        })
        .or_else(|| Slub::new(config, options).ok())
    }

    fn put(&self, config: &CacheConfig, options: PageAllocOptions, mut slub: NonNull<Slub>) {
        let mut guard = self.lock();
        let slub = unsafe { slub.as_mut() };

        // 如果当前 partial list 中对象数量已经超过 min_partial，并且 slub 中没有对象在使用，则销毁该 slub
        if guard.count >= config.min_partial as usize && slub.inner.get_relaxed().inuse == 0 {
            if let Some(_) = slub.try_destroy(&options) {
                return;
            }
        }

        let mut head = unsafe { Pin::new_unchecked(&mut guard.list_head) };
        head.add_tail(slub.get_list());
        guard.count += 1;
    }
}

impl MemCacheNode {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(size_of::<Self>() as u16).unwrap();
    const CONFIG: CacheConfig = CacheConfig::new(c"MemCacheNode", Self::OBJECT_SIZE)
        .ok()
        .unwrap();

    pub(super) fn init(&mut self, config: &CacheConfig, slub: Option<NonNull<Slub>>) {
        *self = Self {
            partial_list: Spinlock::new(PartialList {
                list_head: ListHead::empty(),
                count: 0,
            }),
            object_size: config.object_size,
        };

        unsafe {
            self.partial_list.init_with(|v| {
                let mut head = Pin::new_unchecked(&mut v.list_head);
                head.init();

                if let Some(mut slub) = slub {
                    let slub = slub.as_mut().get_list();
                    head.add_tail(slub);
                    v.count += 1;
                }
            })
        };
    }

    /// 创建一个Slub存放自身，作为 MemCacheNode 类型的 MemCache 的一个节点
    pub(super) fn bootstrap(mut options: PageAllocOptions) -> NonNull<Self> {
        let config = &Self::CONFIG;

        options = options.order(config.frame_order);

        let slub = Slub::new(config, options).unwrap();

        unsafe {
            let mut mem_cache_node: NonNull<Self> =
                slub.as_ref().allocate(NonNull::dangling()).unwrap();

            mem_cache_node.as_mut().init(config, Some(slub));

            mem_cache_node
        }
    }

    /// 从已创建的 MemCacheNode 创建 "mem_cache_node" 的 MemCache
    pub fn bootstrap_cache(
        mem_cache_node: NonNull<Self>,
        mem_cache: &MemCache,
        options: PageAllocOptions,
    ) -> Option<NonNull<MemCache>> {
        let mut mem_cache = mem_cache.allocate::<MemCache>()?;

        unsafe {
            mem_cache
                .as_mut()
                .init(
                    Self::CONFIG,
                    mem_cache_node,
                    options.order(Self::CONFIG.frame_order),
                )
                .ok()?;
        }
        Some(mem_cache)
    }

    /// 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    ///
    /// # Safety
    ///
    /// 确保不会消耗完当前 `Slub`, 否则在该 `Slub` 内对象释放时可能无法恢复到 partial list 上
    pub unsafe fn new_self(
        &self,
        config: &CacheConfig,
        options: PageAllocOptions,
    ) -> Option<NonNull<Self>> {
        assert!(self.object_size.0 == Self::CONFIG.object_size.0);

        let slub = self.get(config, options)?;
        unsafe { slub.as_ref() }
            .allocate(NonNull::dangling())
            .inspect(|_| self.put(config, options, slub))
    }

    #[inline]
    pub fn get(&self, config: &CacheConfig, options: PageAllocOptions) -> Option<NonNull<Slub>> {
        self.partial_list.get(config, options)
    }

    #[inline]
    pub fn put(&self, config: &CacheConfig, options: PageAllocOptions, slub: NonNull<Slub>) {
        self.partial_list.put(config, options, slub);
    }

    pub fn try_destroy(&mut self, options: &PageAllocOptions) -> Option<()> {
        let mut guard = self.partial_list.lock();

        for mut slub in guard.list_head.iter(Slub::list_offset()) {
            let slub = unsafe { slub.as_mut() };
            {
                let mut head = self.partial_list.lock();
                slub.get_list().del(&mut head.list_head);
            }

            slub.try_destroy(options)?;
        }

        Some(())
    }
}
