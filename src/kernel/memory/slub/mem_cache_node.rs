use core::{mem::offset_of, num::NonZeroU16, ops::DerefMut, pin::Pin, ptr::NonNull};

use crate::{
    kernel::memory::{
        frame::Frame,
        page::options::PageAllocOptions,
        slub::{ObjectSize, Slub, calculate_sizes, mem_cache::MemCache},
    },
    lib::rust::{list::ListHead, spinlock::Spinlock},
};

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    pub partial_list: Spinlock<ListHead<Frame>>,
    object_size: ObjectSize,
    object_num: NonZeroU16,
}

impl MemCacheNode {
    const OBJECT_SIZE: NonZeroU16 = NonZeroU16::new(size_of::<Self>() as u16).unwrap();

    fn init(
        &mut self,
        object_size: ObjectSize,
        object_num: NonZeroU16,
        slub: Option<NonNull<Slub>>,
    ) {
        *self = Self {
            partial_list: Spinlock::new(ListHead::empty()),
            object_num,
            object_size,
        };

        unsafe {
            self.partial_list.init_with(|v| {
                let mut head = Pin::new_unchecked(v);
                head.as_mut().init();

                if let Some(mut slub) = slub {
                    let slub = slub.as_mut();
                    if let Slub::Head(_) = slub {
                        let list = Frame::from_child(slub).list.get_mut();

                        head.as_mut().add_head(Pin::new_unchecked(list));
                    }
                }
            })
        };
    }

    /// 创建一个Slub存放自身，作为 MemCacheNode 类型的 MemCache 的一个节点
    pub(super) fn bootstrap(mut options: PageAllocOptions) -> NonNull<Self> {
        let (object_size, object_num, order) = calculate_sizes(Self::OBJECT_SIZE).unwrap();

        options.frame = options.frame.dynamic(order);

        let mut slub = Slub::new(options, object_size, object_num).unwrap();

        unsafe {
            let _slub = slub.as_mut();
            let mut mem_cache_node: NonNull<Self> = match _slub {
                Slub::Head(head) => head.allocate().unwrap(),
                _ => unreachable!(),
            };

            mem_cache_node
                .as_mut()
                .init(object_size, object_num, Some(slub));

            mem_cache_node
        }
    }

    /// 从已创建的 MemCacheNode 创建 "mem_cache_node" 的 MemCache
    pub fn bootstrap_cache(
        mem_cache_node: NonNull<Self>,
        mem_cache: &mut MemCache,
        options: PageAllocOptions,
    ) -> NonNull<MemCache> {
        const NAME: *const u8 = c"mem_cache_node".as_ptr().cast();

        mem_cache.new_boot_from_node(NAME, mem_cache_node, Self::OBJECT_SIZE, options)
    }

    // 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    pub fn new_from_node(
        &mut self,
        object_size: NonZeroU16,
        options: &mut PageAllocOptions,
    ) -> Option<NonNull<MemCacheNode>> {
        let (object_size, object_num, order) =
            calculate_sizes(NonZeroU16::new(object_size.get())?).unwrap();

        options.frame = options.frame.dynamic(order);

        let mut result = self.allocate::<MemCacheNode>(*options)?;

        unsafe {
            let slub = Slub::new(*options, object_size, object_num).ok()?;

            result.as_mut().init(object_size, object_num, Some(slub));
        }

        Some(result)
    }

    pub fn new(node_cache: &mut MemCache, mut options: PageAllocOptions) -> Option<NonNull<Self>> {
        assert!(node_cache.original_size == Self::OBJECT_SIZE);

        let (object_size, object_num, order) = calculate_sizes(Self::OBJECT_SIZE).unwrap();
        options.frame = options.frame.dynamic(order);

        let mut result = node_cache.allocate::<MemCacheNode>()?;

        unsafe {
            let slub = Slub::new(options, object_size, object_num).ok()?;

            result.as_mut().init(object_size, object_num, Some(slub));
        }

        Some(result)
    }

    /// 从`Node`中分配对象
    ///
    /// 默认从`partial_list`中分配对象，若未分配到则尝试创建一个新的`Slub`并从中分配
    ///
    /// 新的`Slub`分配失败时返回`None`
    pub fn allocate<T>(&mut self, options: PageAllocOptions) -> Option<NonNull<T>> {
        if !self.partial_list.get_relaxed().is_empty() {
            let mut partial_list = self.partial_list.lock();

            for mut frame in partial_list.iter(offset_of!(Frame, list)) {
                if let &mut Slub::Head(ref mut head) = unsafe { frame.as_mut().try_into().ok()? } {
                    if let Some(obj) = head.allocate::<T>() {
                        return Some(obj);
                    }
                } else {
                    panic!("Slub in MemCacheNode::partial_list is not SlubType::Head!");
                }
            }
        }

        // 从partial_list中没有分配到，创建一个新的Slub
        let mut new_slub = Slub::new(options, self.object_size, self.object_num).ok()?;

        let new_slub = unsafe { new_slub.as_mut() };
        if let Slub::Head(new_head) = new_slub {
            let mut head = self.partial_list.lock();

            let list = Frame::from_child(new_head).list.get_mut();
            let node = unsafe { Pin::new_unchecked(list) };

            unsafe { Pin::new_unchecked(head.deref_mut()) }.add_head(node);

            new_head.allocate()
        } else {
            panic!("Slub::new() returns a SlubType::Body slub!");
        }
    }

    pub fn try_destroy(&mut self, cache_info: &MemCache) -> Option<()> {
        let mut list = self.partial_list.lock();

        for mut frame in list.iter(offset_of!(Frame, list)) {
            let slub: &mut Slub = unsafe { frame.as_mut() }.try_into().unwrap();
            slub.try_destroy(cache_info)?;

            unsafe { Pin::new_unchecked(&mut *frame.as_mut().list.get()).del() };
        }

        Some(())
    }
}
