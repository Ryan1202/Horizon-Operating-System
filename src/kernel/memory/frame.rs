use core::{
    cell::SyncUnsafeCell,
    fmt::Display,
    mem::{self, ManuallyDrop},
    ops::{Add, DerefMut, Sub},
    ptr::{NonNull, addr_of},
    sync::atomic::AtomicUsize,
};

use crate::{
    CACHELINE_SIZE,
    arch::{PAGE_SIZE, PageLevelEntry, PhyAddr},
    kernel::memory::{
        frame::{
            buddy::{Buddy, BuddyAllocator, FrameOrder},
            reference::{FrameMut, FrameRc, FrameRef},
            zone::ZoneType,
        },
        page::PageTableEntry,
        slub::Slub,
    },
    lib::rust::list::ListNode,
};

pub mod buddy;
pub mod options;
pub mod reference;
pub mod zone;

#[repr(C)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
}

#[unsafe(no_mangle)]
pub static mut PREALLOCATED_END_PHY: PhyAddr = PhyAddr::new(0);

#[cfg(target_pointer_width = "32")]
const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE / 2;
#[cfg(target_pointer_width = "64")]
const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE;

#[derive(Debug, Clone)]
pub enum FrameError {
    IncorrectFrameType,
    OutOfFrames,
    Conflict,
}

#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
pub struct FrameNumber(usize);

impl FrameNumber {
    pub const fn new(num: usize) -> Self {
        FrameNumber(num)
    }

    pub const fn get(&self) -> usize {
        self.0
    }

    pub const fn count_from(self, other: FrameNumber) -> usize {
        self.0.abs_diff(other.0) + 1
    }
}

impl Display for FrameNumber {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "FrameNumber {}", self.0)
    }
}

impl Add<usize> for FrameNumber {
    type Output = FrameNumber;

    fn add(self, rhs: usize) -> Self::Output {
        FrameNumber(self.0 + rhs)
    }
}

impl Sub<usize> for FrameNumber {
    type Output = FrameNumber;

    fn sub(self, rhs: usize) -> Self::Output {
        FrameNumber(self.0 - rhs)
    }
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
    // list 和 xxcount 在前，方便对齐
    pub list: SyncUnsafeCell<ListNode<Frame>>,
    refcount: FrameRc,
    mapcount: AtomicUsize,
    // 其他字段在后
    data: SyncUnsafeCell<FrameData>,
    tag: SyncUnsafeCell<FrameTag>,
}

unsafe impl Sync for Frame {}

pub union FrameData {
    pub unused: (),
    pub range: ManuallyDrop<FrameRange>,
    pub buddy: ManuallyDrop<Buddy>,
    pub slub: ManuallyDrop<Slub>,
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FrameTag {
    /// 暂未使用
    Unused = 0,
    HardwareReserved = 1,
    SystemReserved = 2,
    /// `Free` 是初始化时使用的临时类型，分配器初始化完成后不应再出现该类型的页
    Free = 3,
    /// `Allocated` 用于标识该组页已被分配，但没有标识类型，仍使用 buddy 数据
    Allocated = 4,
    Buddy = 5,
    Slub = 6,
    Io = 7,
}

const _: () = assert!(size_of::<Frame>() <= MAX_PAGE_STRUCT_SIZE);

pub const FRAME_INFO_COUNT: usize = 1 << 20;
const PAGE_INFO_SIZE: usize = FRAME_INFO_COUNT * size_of::<Frame>();

/// Buddy 分配器的虚拟地址存储位置
pub static FRAME_MANAGER: BuddyAllocator = BuddyAllocator::empty();

#[unsafe(no_mangle)]
pub unsafe extern "C" fn page_early_init(
    blocks: *mut E820Ards,
    block_count: u16,
    kernel_start: usize,
    kernel_end: usize,
) {
    unsafe {
        // 都向后对齐到页
        PREALLOCATED_END_PHY = PhyAddr::new(kernel_end).max(PREALLOCATED_END_PHY);

        let kernel_range = (PhyAddr::new(kernel_start), PREALLOCATED_END_PHY);
        Frame::init(blocks, block_count, kernel_range);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn page_init() {
    FRAME_MANAGER.init();
}

// 内核启动早期分配的页都是不会释放的，如页表结构等
#[unsafe(no_mangle)]
pub unsafe extern "C" fn early_allocate_pages(count: u8) -> usize {
    unsafe {
        let addr = PREALLOCATED_END_PHY;
        PREALLOCATED_END_PHY += (count as usize) * 0x1000;

        addr.as_usize()
    }
}

pub static FRAMES: [Frame; PageLevelEntry::MAX + 1] = unsafe { mem::zeroed() };

/// 启动阶段的初始化函数，仅可在单线程环境使用
impl Frame {
    /// 填充 [start, end] 范围的 `Frame`
    ///
    /// 实际上只填写 start 一个 `Frame`
    fn fill_range(start: FrameNumber, end: FrameNumber, e820_type: u32) {
        debug_assert!(start <= end);

        let mut frame = Self::get_mut(start).unwrap();
        let range = ManuallyDrop::new(FrameRange { start, end });
        let data = FrameData { range };

        let tag = match e820_type {
            0 => FrameTag::SystemReserved,
            1 => FrameTag::Free,
            _ => FrameTag::HardwareReserved,
        };

        // 写入首个 page 描述
        unsafe { frame.replace(tag, data) };
    }

    /// 初始化页结构体数组，根据E820内存块信息划分内存，返回页结构体数组所需的总字节数
    fn init(blocks: *mut E820Ards, block_count: u16, kernel_range: (PhyAddr, PhyAddr)) -> usize {
        let (kernel_start, kernel_end) = (
            kernel_range.0.to_frame_number(),
            kernel_range.1.to_frame_number(),
        );

        let mut last = FrameNumber::new(0);
        // 将每个可用内存块按Buddy的方式分割成块
        for i in 0..block_count {
            let block = unsafe { &*blocks.add(i as usize) };

            // 起始地址向后对齐，避免向前越界
            let start_addr = PhyAddr::new(block.base_addr as usize).page_align_up();
            let block_start = start_addr.to_frame_number();

            let length = block.length as usize - start_addr.page_offset();

            let block_end = block_start + frame_count(length);

            if last < block_start {
                // 填充上一个块和当前块之间的空洞为保留
                Self::fill_range(last, block_start - 1, 2);
            }
            last = block_end + 1;

            if block_start > block_end {
                continue;
            }

            // 填充 [block_start, block_end] 范围
            if block_end <= kernel_start || block_start >= kernel_end {
                Self::fill_range(block_start, block_end, block.block_type);
            } else {
                // 内存块和内核有重叠部分
                let e820_type = block.block_type;

                // 前半部分可用
                if block_start < kernel_start {
                    Self::fill_range(block_start, kernel_start - 1, e820_type);
                }

                Self::fill_range(kernel_start, kernel_end, 0);

                // 后半部分可用
                if block_end > kernel_end {
                    Self::fill_range(kernel_end + 1, block_end, e820_type);
                }
            }
        }

        PAGE_INFO_SIZE
    }
}

impl Frame {
    pub fn get_tag(&self) -> FrameTag {
        unsafe { *self.tag.get() }
    }

    pub fn set_tag(&mut self, tag: FrameTag) {
        *self.tag.get_mut() = tag;
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
        let _tag = self.tag.get_mut();
        let _data = self.data.get_mut();
        unsafe {
            match *_tag {
                FrameTag::Buddy => {
                    ManuallyDrop::drop(&mut _data.buddy);
                }
                FrameTag::Free
                | FrameTag::Io
                | FrameTag::SystemReserved
                | FrameTag::HardwareReserved
                | FrameTag::Allocated => {
                    ManuallyDrop::drop(&mut _data.range);
                }
                FrameTag::Slub => {
                    ManuallyDrop::drop(&mut _data.slub);
                }
                FrameTag::Unused => {}
            }
        }

        *_tag = tag;
        *_data = data;
    }
}

/// 工具函数实现
impl Frame {
    pub fn get_tag_relaxed(frame_number: FrameNumber) -> FrameTag {
        assert!(frame_number.0 <= usize::MAX as usize / PAGE_SIZE + 1);
        FRAMES[frame_number.0].get_tag()
    }

    #[inline(always)]
    pub fn get(frame_number: FrameNumber) -> Option<FrameRef> {
        FrameRef::new(Self::get_raw(frame_number))
    }

    #[inline(always)]
    pub fn get_mut(frame_number: FrameNumber) -> Option<FrameMut> {
        FrameMut::new(Self::get_raw(frame_number))
    }

    pub const fn get_raw(frame_number: FrameNumber) -> NonNull<Frame> {
        assert!(frame_number.0 <= usize::MAX as usize / PAGE_SIZE + 1);
        NonNull::from_ref(&FRAMES[frame_number.0])
    }

    #[inline(always)]
    pub fn from_addr<'a>(addr: PhyAddr) -> Option<FrameRef> {
        let page_num = addr.to_frame_number();
        Frame::get(page_num)
    }

    #[inline(always)]
    pub fn from_addr_mut<'a>(addr: PhyAddr) -> Option<FrameMut> {
        let page_num = addr.to_frame_number();
        Frame::get_mut(page_num)
    }

    pub const fn start_addr(&self) -> PhyAddr {
        let frame_number =
            unsafe { (self as *const Frame).offset_from(addr_of!(FRAMES[0])) as usize };
        PhyAddr::new(frame_number * PAGE_SIZE)
    }

    pub fn from_child<'a, T>(child: &'a mut T) -> &'a mut Frame {
        let addr = child as *mut T;
        assert!(addr.addr() >= addr_of!(FRAMES).addr());
        let max = addr_of!(FRAMES[PageLevelEntry::MAX]).addr();
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

    pub fn prev_frame(&mut self, count: usize) -> Option<FrameMut> {
        Frame::get_mut(self.to_frame_number() - count)
    }

    pub fn next_frame(&mut self, count: usize) -> Option<FrameMut> {
        Frame::get_mut(self.to_frame_number() + count)
    }
}

pub const fn frame_count(size: usize) -> usize {
    size.div_ceil(PAGE_SIZE)
}

pub trait FrameAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<FrameMut>;
    fn free(&self, page: &mut Frame) -> Result<usize, FrameError>;
    /// 从 buddy 分配器中剔除指定物理内存区域（如 ioremap 需要映射的物理内存）
    fn assign(&self, start: FrameNumber, len: usize) -> Result<(), FrameError>;
}

#[unsafe(export_name = "allocate_frames")]
pub extern "C" fn allocate_frames_c(zone_type: ZoneType, order: FrameOrder) -> usize {
    FRAME_MANAGER
        .allocate(zone_type, order)
        .map_or(0, |v| v.start_addr().as_usize())
}

#[unsafe(export_name = "free_frames")]
pub extern "C" fn free_frames_c(paddr: usize) -> isize {
    let paddr = PhyAddr::new(paddr);
    let frame = Frame::get_raw(paddr.to_frame_number());
    let mut frame = unsafe { FrameMut::try_from_raw(frame).unwrap() };

    match FRAME_MANAGER.free(frame.deref_mut()) {
        Ok(count) => count as isize,
        Err(_) => -1,
    }
}
