// Basic SLUB-like structures for the kernel (简单骨架)
// - 忽略 NUMA
// - 使用从 C 导入的自旋锁（见 src/include/kernel/spinlock.h）
// 这是最小实现：结构体和基本方法（init/alloc/free stub），便于后续扩展。

use core::{
    cell::SyncUnsafeCell,
    mem::{ManuallyDrop, offset_of},
    num::NonZeroU16,
    ops::DerefMut,
    pin::Pin,
    ptr::NonNull,
};

use crate::{
    CACHELINE_SIZE,
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
        slub::{
            config::CacheConfig,
            mem_cache::{MemCache, MemCaches},
        },
    },
    lib::rust::{list::ListNode, spinlock::Spinlock},
};

pub(super) mod config;
#[cfg(feature = "slub_debug")]
mod debug;
mod mem_cache;
mod mem_cache_node;

pub(super) const MIN_OBJECT_SIZE: usize = 8;
#[cfg(feature = "slub_debug")]
pub(super) const MAX_OBJECT_SIZE: usize = 4096 + CACHELINE_SIZE + debug::RED_ZONE_SIZE;
#[cfg(not(feature = "slub_debug"))]
pub(super) const MAX_OBJECT_SIZE: usize = 4096;
pub(super) const MAX_FRAME_ORDER: FrameOrder = FrameOrder::new(3); // 最大 8 页
pub(super) const MIN_PARTIAL: u8 = 5;
pub(super) const MAX_PARTIAL: u8 = 10;

pub(super) const MIN_ALIGN: usize = core::mem::size_of::<usize>() * 2;

#[repr(transparent)]
#[derive(Clone, Copy, Debug)]
pub struct ObjectSize(pub NonZeroU16);

pub struct Slub {
    list: SyncUnsafeCell<ListNode<Self>>,
    inner: Spinlock<SlubInner>,
}

/// Slub 内部数据结构
#[repr(C, packed)]
pub struct SlubInner {
    /// 空闲对象头
    freelist: Option<NonNull<FreeNode>>,
    /// 正在使用的对象数量
    inuse: u16,
    #[cfg(feature = "slub_debug")]
    /// 到用户数据的起始位置的偏移量
    user_offset: u8,
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
        config: &CacheConfig,
        options: PageAllocOptions,
    ) -> Result<NonNull<Self>, MemoryError> {
        // 分配页
        let mut pages = options.allocate()?;

        let start_addr = pages.start_addr();
        let free_nodes = unsafe { start_addr.as_mut_ptr::<FreeNode>().as_mut().unwrap() };

        FreeNode::init(free_nodes, config);

        let freelist = NonNull::from(free_nodes);
        #[cfg(feature = "slub_debug")]
        let freelist = unsafe {
            use crate::kernel::memory::slub::debug::user_ptr_offset;
            freelist.byte_add(user_ptr_offset(config.align))
        };

        let first_frame = pages.get_frame().unwrap();

        let slub_info = Slub {
            list: SyncUnsafeCell::new(ListNode::new()),
            inner: Spinlock::new(SlubInner {
                freelist: Some(freelist),
                inuse: 0,
                #[cfg(feature = "slub_debug")]
                user_offset: config.user_offset as u8,
            }),
        };

        let slub = {
            set_slub(first_frame, slub_info);

            let slub: &mut Self = first_frame.deref_mut().try_into()?;
            NonNull::from_ref(slub)
        };

        Ok(slub)
    }

    pub fn new(
        config: &CacheConfig,
        options: PageAllocOptions,
    ) -> Result<NonNull<Self>, MemoryError> {
        Self::init_slab(config, options)
    }

    /// 销毁当前 `Slub`
    pub fn try_destroy(&mut self, cache_info: &MemCache) -> Option<()> {
        if unsafe { self.list.get().as_ref()? }.is_linked() {
            return None;
        }

        self.inner.lock().is_destroyable()?;

        let alloc_type = cache_info.options.frame.get_type();
        let order = if let FrameAllocType::Dynamic { order } = alloc_type {
            *order
        } else {
            unreachable!("Slub cache should always use dynamic frame allocation!");
        };

        let frame = unsafe { Frame::from_child(self) };

        let zone_type = ZoneType::from_address(frame.start_addr());
        let buddy = ManuallyDrop::new(Buddy::new(order, zone_type));

        unsafe { frame.replace(FrameTag::Allocated, FrameData { buddy }) };
        Some(())
    }

    #[inline]
    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        self.inner.lock().allocate()
    }

    #[inline]
    pub fn deallocate(&mut self, obj: NonNull<u8>) {
        self.inner.lock().deallocate(obj);
    }

    fn get_list(&mut self) -> Pin<&mut ListNode<Self>> {
        unsafe { Pin::new_unchecked(self.list.get_mut()) }
    }

    const fn list_offset() -> usize {
        offset_of!(Self, list)
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

        #[cfg(feature = "slub_debug")]
        unsafe {
            use crate::kernel::memory::slub::debug::{
                RED_ZONE_SIZE, check_poison, check_red_zones, poison_on_alloc, user_ptr_offset,
            };

            let user_ptr = freelist.as_ptr() as *mut u8;
            let user_offset = user_ptr_offset(self.user_offset as usize);
            let ptr = user_ptr.byte_sub(user_offset);
            let user_size = self.object_size.get() as usize - user_offset - RED_ZONE_SIZE;

            check_poison(user_ptr, user_size);
            check_red_zones(ptr, user_offset, user_size);
            poison_on_alloc(user_ptr, user_size);
        }

        Some(node.cast())
    }

    pub fn deallocate<T>(&mut self, obj: NonNull<T>) {
        let node = obj.cast::<FreeNode>();

        #[cfg(feature = "slub_debug")]
        unsafe {
            use crate::kernel::memory::slub::debug::{
                RED_ZONE_SIZE, check_red_zones, poison_on_free, user_ptr_offset,
            };

            let user_ptr = node.as_ptr() as *mut u8;
            let user_offset = user_ptr_offset(self.user_offset as usize);
            let ptr = user_ptr.byte_sub(user_offset);
            let user_size = self.object_size.get() as usize - user_offset - RED_ZONE_SIZE;

            check_red_zones(ptr, user_offset, user_size);
            poison_on_free(user_ptr, user_size);
        }

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

fn set_slub(frame: &mut Frame, slub_info: Slub) {
    unsafe {
        frame.replace(
            FrameTag::Slub,
            FrameData {
                slub: ManuallyDrop::new(slub_info),
            },
        )
    };
}

impl FreeNode {
    pub fn init(&mut self, config: &CacheConfig) {
        let mut current = NonNull::from(self);

        #[cfg(feature = "slub_debug")]
        let user_offset = debug::user_ptr_offset(config.align);

        #[cfg(feature = "slub_debug")]
        unsafe {
            current = current.byte_add(user_offset)
        };

        unsafe {
            for _ in 0..(config.object_num.get() - 1) {
                #[cfg(feature = "slub_debug")]
                {
                    use crate::kernel::memory::slub::debug::{
                        RED_ZONE_SIZE, init_red_zones, poison_on_free,
                    };

                    let user_ptr = current.as_ptr() as *mut u8;
                    let user_size =
                        config.object_size.0.get() as usize - user_offset - RED_ZONE_SIZE;

                    let slab_obj_start = user_ptr.byte_sub(user_offset);
                    init_red_zones(slab_obj_start, user_offset, user_size);
                    poison_on_free(user_ptr, user_size);
                }
                let next = current.byte_add(config.object_size.0.get() as usize);

                current.write(FreeNode { next: Some(next) });
                current = next;
            }

            #[cfg(feature = "slub_debug")]
            {
                use crate::kernel::memory::slub::debug::{
                    RED_ZONE_SIZE, init_red_zones, poison_on_free,
                };

                let user_ptr = current.as_ptr() as *mut u8;
                let user_size = config.object_size.0.get() as usize - user_offset - RED_ZONE_SIZE;

                let slab_obj_start = user_ptr.byte_sub(user_offset);
                init_red_zones(slab_obj_start, user_offset, user_size);
                poison_on_free(user_ptr, user_size);
            }
            current.write(FreeNode { next: None });
        }
    }
}

const fn calculate_order(size: ObjectSize) -> Result<FrameOrder, SlubError> {
    let mut size = size.0.get().next_multiple_of(size_of::<usize>() as u16);
    size = size.clamp(MIN_OBJECT_SIZE as u16, MAX_OBJECT_SIZE as u16);

    let mut order = MAX_FRAME_ORDER.get();
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
    align: usize,
) -> Result<(ObjectSize, NonZeroU16, FrameOrder, usize), SlubError> {
    let align = calculate_alignment(size.get() as usize, align);

    let size = size.get() as usize;

    #[cfg(feature = "slub_debug")]
    let size = size + (debug::user_ptr_offset(align) + debug::RED_ZONE_SIZE);

    let object_size = NonZeroU16::new((size - 1).next_multiple_of(align) as u16)
        .expect("`object_size` should be non zero after alignment!");

    let object_size = ObjectSize(object_size);
    let order = calculate_order(object_size)?;

    let slab_size = ArchPageTable::PAGE_SIZE << order.get();

    let object_num = (slab_size / object_size.0.get() as usize) as u16;

    Ok((
        object_size,
        NonZeroU16::new(object_num).expect("`object_num` should be non zero after calculation!"),
        order,
        align,
    ))
}

const fn calculate_alignment(size: usize, align: usize) -> usize {
    let mut align = (align - 1).next_power_of_two();
    align = align.max(1 << size.trailing_zeros());
    align.clamp(MIN_ALIGN, CACHELINE_SIZE)
}

#[unsafe(no_mangle)]
pub extern "C" fn mem_caches_init() {
    MemCaches::init();
}
