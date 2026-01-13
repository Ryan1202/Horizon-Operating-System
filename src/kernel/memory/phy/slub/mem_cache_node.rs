use core::{num::NonZeroU16, ops::DerefMut, pin::Pin, ptr::NonNull};

use crate::{
    kernel::memory::phy::{
        page::{Frame, FrameNumber, FrameTag, ZoneType, buddy::PageOrder, page_align_down},
        slub::{ObjectSize, Slub, calculate_sizes, mem_cache::MemCache},
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
    list_for_each_owner,
};

/// 每个节点的缓存（这里我们忽略节点/NUMA，仅保留一个节点结构）
pub struct MemCacheNode {
    pub partial_list: Spinlock<ListHead<Frame>>,
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
    pub(super) fn bootstrap() -> NonNull<Self> {
        let (object_size, object_num, order) = calculate_sizes(Self::OBJECT_SIZE).unwrap();

        let mut slub = Slub::new(ZoneType::LinearMem, object_size, object_num, order).unwrap();

        unsafe {
            let _slub = slub.as_mut();
            let mut mem_cache_node: NonNull<Self> = match _slub {
                Slub::Head(head) => head.allocate().unwrap(),
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
            let partial_list = self.partial_list.lock();
            let head = unsafe { Pin::new_unchecked(&*partial_list) };

            list_for_each_owner!(frame, Frame, list, head, {
                if let Slub::Head(head) = Slub::from_frame(unsafe { frame.as_mut() }) {
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
        // 先找到所在页的 Page 结构，再从其中取出 Slub 地址
        let frame_number = FrameNumber::from_addr(page_align_down(obj.addr().get()));
        let frame = Frame::from_frame_number(frame_number);

        if let FrameTag::Slub = frame.get_tag() {
            let slub = Slub::from_frame(frame);
            match slub {
                Slub::Body { offset } => {
                    // 如果是Body就重新定位到Head
                    let head = Frame::from_frame_number(frame_number - *offset as usize);
                    let head = Slub::from_frame(head);
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
