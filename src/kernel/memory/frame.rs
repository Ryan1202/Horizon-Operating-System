use core::{
    cell::SyncUnsafeCell,
    mem::{self, ManuallyDrop, transmute},
    ptr::{NonNull, addr_of},
    sync::atomic::{AtomicU8, AtomicUsize, Ordering},
};

use crate::{
    CACHELINE_SIZE,
    arch::{ArchPageTable, PhysAddr},
    kernel::memory::{
        arch::ArchMemory,
        frame::{
            anonymous::Anonymous,
            buddy::{Buddy, BuddyAllocator, FrameOrder},
            reference::{FrameRc, SharedFrames, UniqueFrames},
            zone::ZoneType,
        },
        slub::Slub,
    },
};

pub mod anonymous;
pub mod buddy;
mod early;
pub mod number;
pub mod options;
pub mod reference;
pub mod zone;

pub use number::FrameNumber;

#[unsafe(export_name = "total_pages")]
pub static TOTAL_PAGES: AtomicUsize = AtomicUsize::new(0);
#[unsafe(export_name = "allocated_pages")]
pub static ALLOCATED_PAGES: AtomicUsize = AtomicUsize::new(0);

#[unsafe(no_mangle)]
pub static PREALLOCATED_END_PHY: SyncUnsafeCell<PhysAddr> = SyncUnsafeCell::new(PhysAddr::new(0));

#[cfg(target_pointer_width = "32")]
const MAX_METADATA_SIZE: usize = CACHELINE_SIZE / 2;
#[cfg(target_pointer_width = "64")]
const MAX_METADATA_SIZE: usize = CACHELINE_SIZE;

#[derive(Debug, Clone)]
pub enum FrameError {
    IncorrectFrameType,
    OutOfFrames,
    Conflict,
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct FrameRange {
    pub start: FrameNumber,
    pub end: FrameNumber,
}

#[repr(C)]
#[cfg_attr(target_pointer_width = "32", repr(align(32)))]
#[cfg_attr(target_pointer_width = "64", repr(align(64)))]
pub struct Frame {
    refcount: FrameRc,
    // 其他字段在后
    data: SyncUnsafeCell<FrameData>,
    tag: AtomicU8,
}

pub union FrameData {
    pub unused: (),
    pub range: FrameRange,
    pub buddy: ManuallyDrop<Buddy>,
    pub(super) slub: ManuallyDrop<Slub>,
    pub anonymous: ManuallyDrop<Anonymous>,
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FrameTag {
    /// 不可使用（比如为一组页中非首个页）
    Unavailable = 0,
    HardwareReserved = 1,
    SystemReserved = 2,
    /// `Free` 是初始化时使用的临时类型，分配器初始化完成后不应再出现该类型的页
    Free = 3,
    /// `Anonymous` 用于标识该组页已被分配，但没有标识类型
    Anonymous = 4,
    Buddy = 5,
    Slub = 6,
    /// 跨越单个物理页的大页中，使用头一个页存放元数据，其余页使用 `Tail` 标记
    Tail = 7,
}

const _: () = assert!(size_of::<Frame>() <= MAX_METADATA_SIZE);

pub const FRAME_INFO_COUNT: usize = 1 << 20;
const PAGE_INFO_SIZE: usize = FRAME_INFO_COUNT * size_of::<Frame>();

/// Buddy 分配器的虚拟地址存储位置
pub static FRAME_MANAGER: BuddyAllocator = BuddyAllocator::empty();

pub static FRAMES: [Frame; ArchPageTable::TOTAL_ENTRIES] = unsafe { mem::zeroed() };

impl Frame {
    pub fn get_tag(&self) -> FrameTag {
        unsafe { transmute(self.tag.load(Ordering::Acquire)) }
    }

    pub fn set_tag(&mut self, tag: FrameTag) {
        self.tag.store(unsafe { transmute(tag) }, Ordering::Release);
    }

    pub fn get_data(&self) -> &FrameData {
        unsafe { &*self.data.get() }
    }

    pub fn get_data_mut(&mut self) -> &mut FrameData {
        self.data.get_mut()
    }

    /// 替换 Frame 的 tag 和 data，自动释放旧数据
    ///
    /// # Safety
    ///
    /// 调用者必须确保 Frame 当前的 tag 和 data 是匹配且有效的
    pub unsafe fn replace(&mut self, tag: FrameTag, data: FrameData) {
        let _tag = self.get_tag();
        let _data = self.data.get_mut();
        unsafe {
            match _tag {
                FrameTag::Anonymous => {
                    ManuallyDrop::drop(&mut _data.anonymous);
                }
                FrameTag::Buddy => {
                    ManuallyDrop::drop(&mut _data.buddy);
                }
                FrameTag::Free
                | FrameTag::SystemReserved
                | FrameTag::HardwareReserved
                | FrameTag::Tail => {}
                FrameTag::Slub => {
                    ManuallyDrop::drop(&mut _data.slub);
                }
                FrameTag::Unavailable => {}
            }
        }

        *_data = data;
        self.set_tag(tag);
    }
}

/// 工具函数实现
impl Frame {
    pub fn get_tag_relaxed(frame_number: FrameNumber) -> FrameTag {
        assert!(frame_number.get() <= usize::MAX / ArchPageTable::PAGE_SIZE + 1);
        unsafe { transmute(FRAMES[frame_number.get()].tag.load(Ordering::Relaxed)) }
    }

    #[inline(always)]
    pub fn get(frame_number: FrameNumber) -> Option<SharedFrames> {
        SharedFrames::new(Self::get_raw(frame_number))
    }

    pub const fn get_raw(frame_number: FrameNumber) -> NonNull<Frame> {
        assert!(frame_number.get() <= usize::MAX / ArchPageTable::PAGE_SIZE + 1);
        NonNull::from_ref(&FRAMES[frame_number.get()])
    }

    #[inline(always)]
    pub fn from_addr(addr: PhysAddr) -> Option<SharedFrames> {
        let page_num = addr.to_frame_number();
        Frame::get(page_num)
    }

    pub const fn start_addr(&self) -> PhysAddr {
        let frame_number =
            unsafe { (self as *const Frame).offset_from(addr_of!(FRAMES[0])) as usize };
        PhysAddr::new(frame_number * ArchPageTable::PAGE_SIZE)
    }

    /// 从子对象指针获取父对象指针
    ///
    /// # Safety
    ///
    /// 调用者必须确保 `child` 确实是 `Frame` 内部某个字段的指针
    pub unsafe fn from_child<T>(child: &mut T) -> &mut Frame {
        let addr = child as *mut T;
        assert!(addr.addr() >= addr_of!(FRAMES).addr());
        let max = addr_of!(FRAMES[ArchPageTable::TOTAL_ENTRIES - 1]).addr();
        assert!(addr.addr() < max + size_of::<Frame>());
        unsafe {
            addr.map_addr(|v| v & !(align_of::<Self>() - 1))
                .cast::<Self>()
                .as_mut()
                .unwrap()
        }
    }

    pub fn to_frame_number(&self) -> FrameNumber {
        let frame_number =
            unsafe { (self as *const Frame).offset_from(addr_of!(FRAMES[0])) as usize };
        FrameNumber::new(frame_number)
    }

    pub fn prev_frame(&mut self, count: usize) -> NonNull<Frame> {
        Frame::get_raw(self.to_frame_number() - count)
    }

    pub fn next_frame(&mut self, count: usize) -> NonNull<Frame> {
        Frame::get_raw(self.to_frame_number() + count)
    }
}

pub const fn frame_count(size: usize) -> usize {
    size.div_ceil(ArchPageTable::PAGE_SIZE)
}

pub trait FrameAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<UniqueFrames>;
    fn deallocate(&self, page: &mut Frame) -> Result<usize, FrameError>;
    /// 从 buddy 分配器中剔除指定物理内存区域（如 ioremap 需要映射的物理内存）
    ///
    /// 需要注意：`start` 须对齐到 `order`
    fn assign(&self, start: FrameNumber, order: FrameOrder) -> Result<UniqueFrames, FrameError>;
}

#[unsafe(export_name = "assign_frames")]
fn assign_frames_c(paddr: usize, count: usize) {
    let paddr = PhysAddr::new(paddr);
    if !paddr.is_page_aligned() {
        printk!(
            "assign_frames(): WARNING: paddr is not page aligned! paddr: {}",
            paddr.as_usize()
        );
    }

    let frame_number = paddr.to_frame_number();

    let mut start = frame_number;
    let end = start + count;
    while start < end {
        let order = FrameOrder::new(start.get().trailing_zeros() as u8);

        let _ = FRAME_MANAGER
            .assign(start, order)
            .map(|frame| mem::forget(frame))
            .inspect_err(|e| {
                printk!(
                    "Assign frame error! error:{:?}, paddr:{}, count:{}",
                    e,
                    paddr,
                    count
                )
            });

        start = start + order.to_count().get();
    }
}
