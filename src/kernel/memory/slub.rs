// Basic SLUB-like structures for the kernel (简单骨架)
// - 忽略 NUMA
// - 使用从 C 导入的自旋锁（见 src/include/kernel/spinlock.h）
// 这是最小实现：结构体和基本方法（init/alloc/free stub），便于后续扩展。

use core::{mem::ManuallyDrop, num::NonZeroU16, ops::DerefMut, ptr::NonNull};

use crate::{
    arch::ArchPageTable,
    kernel::memory::{
        MemoryError,
        arch::ArchMemory,
        frame::{
            Frame, FrameData, FrameError, FrameTag,
            buddy::{Buddy, FrameOrder},
            options::{FrameAllocOptions, FrameAllocType},
            zone::ZoneType,
        },
        page::options::PageAllocOptions,
        slub::mem_cache::{MemCache, MemCaches},
    },
};

pub(super) mod config;
mod mem_cache;
mod mem_cache_node;

pub(super) const MIN_OBJECT_SIZE: usize = 8;
pub(super) const MAX_OBJECT_SIZE: usize = 4096;
pub(super) const MAX_FRAME_ORDER: FrameOrder = FrameOrder::new(3); // 最大 8 页
pub(super) const MIN_PARTIAL: u8 = 5;
pub(super) const MAX_PARTIAL: u8 = 10;

pub(super) const ALIGN: usize = core::mem::size_of::<usize>();

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct ObjectSize(pub NonZeroU16);

#[repr(u8)]
pub enum Slub {
    Head(SlubInner),
    Body { offset: u8 },
}

/// Slub 内部数据结构
#[repr(C, packed)]
pub struct SlubInner {
    /// 空闲对象头
    freelist: Option<NonNull<FreeNode>>,
    /// 正在使用的对象数量
    inuse: u16,
    /// object总数量
    object_size: NonZeroU16,
    // /// 所属内存区域类型
    // zone_type: ZoneType,
}

#[repr(C)]
pub struct FreeNode {
    pub next: Option<NonNull<FreeNode>>,
}
const _: () = assert!(size_of::<FreeNode>() <= MIN_OBJECT_SIZE);

#[derive(Debug)]
pub enum SlubError {
    /// 请求的对象大小超过允许的最大值`MAX_OBJECT_SIZE`
    ///
    /// `TooLargeSize(size: usize)`
    TooLargeSize(usize),

    /// 请求的对象大小导致内存浪费过多(超过1/2组页)
    ///
    /// `TooMuchWaste(waste: usize, size: usize)`
    TooMuchWaste(usize, usize),

    /// 分配页失败（可能是当前Zone内存不足）
    ///
    /// `FrameAllocationFailed(options: FrameAllocOptions)`
    FrameAllocationFailed(FrameAllocOptions, FrameError),

    /// `Slub`调用`Buddy`释放页时，传入了非`FrameTag::Buddy`类型的页
    ///
    /// `IncorrectAddressToFree(addr: NonNull<Frame>)`
    IncorrectAddressToFree(NonNull<Frame>),
}

impl Slub {
    // 通用初始化逻辑
    fn init_slab(
        options: PageAllocOptions,
        object_num: NonZeroU16,
        object_size: NonZeroU16,
    ) -> Result<NonNull<Self>, MemoryError> {
        // 分配页
        let mut pages = options.allocate()?;
        let page_count = pages.get_count();

        let start_addr = pages.start_addr();
        let free_nodes = unsafe { start_addr.as_mut_ptr::<FreeNode>().as_mut().unwrap() };

        FreeNode::init(free_nodes, object_num, object_size);

        let first_frame = pages.get_frame().unwrap();

        let data = FrameData {
            slub: ManuallyDrop::new(Slub::Head(SlubInner {
                freelist: Some(NonNull::from(free_nodes)),
                inuse: 0,
                object_size,
                // ZoneType::from_address(first_frame.start_addr()),
            })),
        };

        let slub = {
            unsafe { first_frame.replace(FrameTag::Slub, data) };

            first_frame.list.get_mut().init();

            let slub: &mut Self = first_frame.deref_mut().try_into()?;
            NonNull::from(slub)
        };

        // 填写 Frame 中的 Slub
        for i in 1..page_count {
            let mut frame = first_frame.next_frame(i).unwrap();

            let slub_info = Slub::Body { offset: i as u8 };

            unsafe {
                frame.replace(
                    FrameTag::Slub,
                    FrameData {
                        slub: ManuallyDrop::new(slub_info),
                    },
                )
            };
        }

        Ok(slub)
    }

    pub fn new(
        options: PageAllocOptions,
        object_size: ObjectSize,
        object_num: NonZeroU16,
    ) -> Result<NonNull<Self>, MemoryError> {
        Self::init_slab(options, object_num, object_size.0)
    }

    pub fn try_destroy(&mut self, cache_info: &MemCache) -> Option<()> {
        if let Slub::Head(head) = self {
            head.is_destroyable()?;
        } else {
            unreachable!("Only Slub heads can be destroyed!");
        }

        let alloc_type = cache_info.options.frame.get_type();
        let order = if let FrameAllocType::Dynamic { order } = alloc_type {
            *order
        } else {
            unreachable!("Slub cache should always use dynamic frame allocation!");
        };

        unsafe {
            let frame = Frame::from_child(self);

            let zone_type = ZoneType::from_address(frame.start_addr());
            let buddy = ManuallyDrop::new(Buddy { order, zone_type });

            frame.replace(FrameTag::Allocated, FrameData { buddy });
        }
        Some(())
    }

    #[inline]
    pub fn deallocate(&mut self, obj: NonNull<u8>) -> Result<(), MemoryError> {
        let frame = Frame::from_child(unsafe { (self as *mut Self).as_mut().unwrap() });
        match self {
            Slub::Head(head) => Ok(head.deallocate(obj)),
            Slub::Body { offset } => {
                let mut head = frame.prev_frame(*offset as usize).unwrap();

                let head = head.deref_mut().try_into()?;

                if let &mut Slub::Head(ref mut head) = head {
                    Ok(head.deallocate(obj))
                } else {
                    unreachable!("Slub body does not point to a head!");
                }
            }
        }
    }
}

impl<'a> TryFrom<&'a mut Frame> for &'a mut Slub {
    type Error = FrameError;

    fn try_from(value: &'a mut Frame) -> Result<Self, Self::Error> {
        (value.get_tag() == FrameTag::Slub)
            .then(|| unsafe { value.get_data_mut().slub.deref_mut() })
            .ok_or(FrameError::IncorrectFrameType)
    }
}

impl SlubInner {
    pub const fn get_inuse_relaxed(&self) -> u16 {
        self.inuse
    }

    /// 返回一个类型为`T`的对象的指针，内存大小在创建`Slub`时已确定
    ///
    /// 如果该`Slub`没有剩余可用对象，则返回`None`
    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        let freelist = self.freelist?;

        let node = freelist;
        self.freelist = unsafe { node.read() }.next;
        self.inuse += 1;
        Some(node.cast())
    }

    pub fn deallocate<T>(&mut self, obj: NonNull<T>) {
        let node = obj.cast::<FreeNode>();

        unsafe {
            node.write(FreeNode {
                next: self.freelist,
            });
        }

        self.freelist = Some(node);
        self.inuse -= 1;
    }

    pub const fn is_destroyable(&self) -> Option<()> {
        if self.inuse == 0 { Some(()) } else { None }
    }
}

impl FreeNode {
    pub fn init(&mut self, object_num: NonZeroU16, object_size: NonZeroU16) {
        let mut current = NonNull::from(self);

        unsafe {
            for _ in 0..(object_num.get() - 1) {
                let next = current.byte_add(object_size.get() as usize);
                current.write(FreeNode { next: Some(next) });
                current = next;
            }

            current.write(FreeNode { next: None });
        }
    }
}

const fn calculate_order(size: ObjectSize) -> Result<FrameOrder, SlubError> {
    let mut size = size.0.get().next_multiple_of(size_of::<usize>() as u16);
    size = size.min(MAX_OBJECT_SIZE as u16).max(MIN_OBJECT_SIZE as u16);

    let mut order = 0;
    let fractions = [16, 8, 4, 2];
    let mut i = 0;

    while i < fractions.len() {
        let fraction = fractions[i];
        i += 1;

        let mut _order = 0;
        while _order < MAX_FRAME_ORDER.get() {
            let slab_size = ArchPageTable::PAGE_SIZE << _order;

            let remain = slab_size % (size as usize);
            if remain < (slab_size / fraction) {
                order = _order;
                break;
            }
            _order += 1;
        }

        if order < MAX_FRAME_ORDER.get() {
            return Ok(FrameOrder::new(order as u8));
        }
    }

    if size <= MAX_OBJECT_SIZE as u16 {
        Err(SlubError::TooMuchWaste(
            (ArchPageTable::PAGE_SIZE << order) % (size as usize),
            size as usize,
        ))
    } else {
        Err(SlubError::TooLargeSize(size as usize))
    }
}

const fn calculate_sizes(
    size: NonZeroU16,
) -> Result<(ObjectSize, NonZeroU16, FrameOrder), SlubError> {
    let object_size = NonZeroU16::new(size.get().next_multiple_of(size_of::<usize>() as u16))
        .expect("`object_size` should be non zero after alignment!");
    let object_size = ObjectSize(object_size);
    let order = calculate_order(object_size)?;

    let slab_size = ArchPageTable::PAGE_SIZE << order.get();

    let object_num = (slab_size / object_size.0.get() as usize) as u16;

    Ok((
        object_size,
        NonZeroU16::new(object_num).expect("`object_num` should be non zero after calculation!"),
        order,
    ))
}

#[unsafe(no_mangle)]
pub extern "C" fn mem_caches_init() {
    MemCaches::init();
}
