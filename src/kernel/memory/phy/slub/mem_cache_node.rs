use core::{mem::offset_of, num::NonZeroU16, ops::DerefMut, pin::Pin, ptr::NonNull};

use crate::{
    kernel::memory::{
        phy::{
            frame::{Frame, FrameNumber, FrameTag, frame_align_down},
            slub::{ObjectSize, Slub, calculate_sizes, mem_cache::MemCache},
        },
        vir::page::options::PageAllocOptions,
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
        const NAME: *const u8 = b"mem_cache_node\0".as_ptr();

        mem_cache.new_boot_from_node(NAME, mem_cache_node, Self::OBJECT_SIZE, options)
    }

    // 从分配 MemCacheNode 的 Node 中分配用于其他类型的 MemCacheNode 对象
    pub fn new(
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

    /// 从`Node`中分配对象
    ///
    /// 默认从`partial_list`中分配对象，若未分配到则尝试创建一个新的`Slub`并从中分配
    ///
    /// 新的`Slub`分配失败时返回`None`
    pub fn allocate<T>(&mut self, options: PageAllocOptions) -> Option<NonNull<T>> {
        if !self.partial_list.get_atomic_snapshot().is_empty() {
            let mut partial_list = self.partial_list.lock();

            for mut frame in partial_list.iter(offset_of!(Frame, list)) {
                if let Slub::Head(head) = Slub::from_frame(unsafe { frame.as_mut() }) {
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

    pub fn free<T>(&mut self, obj: NonNull<T>) -> Option<()> {
        // 先找到所在页的 Frame 结构，再从其中取出 Slub 地址
        let frame_number = FrameNumber::from_addr(frame_align_down(obj.addr().get()));
        let mut frame = Frame::get_mut(frame_number)?;

        if let FrameTag::Slub = frame.get_tag() {
            let slub = Slub::from_frame(frame.deref_mut());
            match slub {
                Slub::Body { offset } => {
                    // 如果是Body就重新定位到Head
                    let mut head = Frame::get_mut(frame_number - *offset as usize)?;
                    let head = Slub::from_frame(head.deref_mut());
                    if let Slub::Head(head) = head {
                        head.free(obj);
                    } else {
                        unreachable!("Slub body does not point to a head!");
                    }
                }
                Slub::Head(head) => head.free(obj),
            };
        }

        Some(())
    }
}
