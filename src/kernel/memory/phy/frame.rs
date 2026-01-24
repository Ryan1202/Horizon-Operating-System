use core::{
    cell::SyncUnsafeCell,
    mem::{self, ManuallyDrop},
    ops::{Add, Sub},
    ptr::NonNull,
};

use crate::{
    CACHELINE_SIZE,
    arch::x86::kernel::page::{PAGE_SIZE, PageLevelEntry},
    kernel::memory::{
        VIR_BASE, VIR_BASE_ADDR,
        page::PageTableEntry,
        phy::{
            E820Ards, PREALLOCATED_END_PHY,
            frame::{
                buddy::{Buddy, BuddyAllocator, FrameOrder},
                zone::ZoneType,
            },
            slub::Slub,
        },
    },
    lib::rust::list::ListNode,
};

pub mod buddy;
pub mod options;
pub mod zone;

#[cfg(target_pointer_width = "32")]
const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE / 2;
#[cfg(target_pointer_width = "64")]
const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE;

#[derive(Debug)]
pub enum FrameError {
    IncorrectFrameType,
    OutOfFrames,
}

#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
pub struct FrameNumber(usize);

impl FrameNumber {
    pub const fn new(num: usize) -> Self {
        FrameNumber(num)
    }

    pub const fn from_addr(addr: usize) -> Self {
        FrameNumber(addr / PAGE_SIZE)
    }

    pub const fn get(&self) -> usize {
        self.0
    }

    pub const fn count_from(self, other: FrameNumber) -> usize {
        if self.0 >= other.0 {
            self.0 - other.0 + 1
        } else {
            other.0 - self.0 + 1
        }
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

#[derive(Clone)]
#[repr(C)]
pub struct FrameRange {
    pub start: FrameNumber,
    pub end: FrameNumber,
}

#[repr(C)]
#[cfg_attr(target_pointer_width = "32", repr(align(32)))]
#[cfg_attr(target_pointer_width = "64", repr(align(64)))]
pub struct Frame {
    // list 在前，8/16字节对齐
    pub(super) list: SyncUnsafeCell<ListNode<Frame>>,
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
    pub vmalloc: ManuallyDrop<(FrameRange, Option<NonNull<Frame>>)>,
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
    /// `Allocated` 用于标识该组页已被分配，但没有标识类型，需要搭配range使用
    Allocated = 4,
    Buddy = 5,
    Slub = 6,
    Io = 7,
    Vmalloc = 8,
}

const _: () = assert!(size_of::<Frame>() <= MAX_PAGE_STRUCT_SIZE);

pub const FRAME_INFO_COUNT: usize = 1 << 20;
const PAGE_INFO_SIZE: usize = FRAME_INFO_COUNT * size_of::<Frame>();

/// Buddy 分配器的虚拟地址存储位置
static FRAME_MANAGER: BuddyAllocator = BuddyAllocator::empty();

#[unsafe(no_mangle)]
pub unsafe extern "C" fn page_early_init(
    blocks: *mut E820Ards,
    block_count: u16,
    kernel_start: usize,
    kernel_end: usize,
) {
    unsafe {
        // 都向后对齐到页
        // 只是为了看着稍微舒服一点
        PREALLOCATED_END_PHY = frame_align_up(PREALLOCATED_END_PHY.max(kernel_end));

        VIR_BASE_ADDR = &VIR_BASE as *const _ as usize;

        Frame::init(blocks, block_count, (kernel_start, PREALLOCATED_END_PHY));
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
        addr
    }
}

pub static mut FRAMES: [Frame; PageLevelEntry::MAX + 1] = unsafe { mem::zeroed() };

/// 启动阶段的初始化函数，仅可在单线程环境使用
impl Frame {
    /// 填充 [start, end] 范围的Page
    fn fill_range(start: FrameNumber, end: FrameNumber, e820_type: u32) {
        let frame = Self::from_frame_number(start);
        let range = FrameRange { start, end };
        let data = FrameData {
            range: ManuallyDrop::new(range),
        };

        let tag = match e820_type {
            0 => FrameTag::SystemReserved,
            1 => FrameTag::Free,
            _ => FrameTag::HardwareReserved,
        };

        // 写入首个 page 描述
        unsafe { frame.replace(tag, data) };
    }

    /// 初始化页结构体数组，根据E820内存块信息划分内存，返回页结构体数组所需的总字节数
    fn init(blocks: *mut E820Ards, block_count: u16, kernel_range: (usize, usize)) -> usize {
        let (kernel_start, kernel_end) = (
            FrameNumber::from_addr(kernel_range.0),
            FrameNumber::from_addr(kernel_range.1),
        );

        let mut last = FrameNumber::new(0);
        // 将每个可用内存块按Buddy的方式分割成块
        for i in 0..block_count {
            let block = unsafe { &*blocks.add(i as usize) };

            // 起始地址向后对齐，避免向前越界
            let start_addr = frame_align_up(block.base_addr as usize);
            let block_start = FrameNumber::from_addr(start_addr);
            let length =
                frame_align_down(block.length as usize - (start_addr - (block.base_addr as usize)));
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
                if block_start < kernel_start {
                    // 前半部分可用
                    Self::fill_range(block_start, kernel_start - 1, block.block_type);
                }
                Self::fill_range(kernel_start, kernel_end, 0);
                if block_end > kernel_end {
                    // 后半部分可用
                    Self::fill_range(kernel_end + 1, block_end, block.block_type);
                }
            }
        }

        PAGE_INFO_SIZE
    }

    pub fn free(&mut self) -> Result<(), FrameError> {
        FRAME_MANAGER.free(self)
    }
}

impl Frame {
    pub fn get_tag(&self) -> FrameTag {
        unsafe { *self.tag.get() }
    }

    pub fn set_tag(&mut self, tag: FrameTag) {
        *self.tag.get_mut() = tag;
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
                FrameTag::Vmalloc => {
                    ManuallyDrop::drop(&mut _data.vmalloc);
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
    #[inline(always)]
    pub fn from_frame_number<'a>(frame_number: FrameNumber) -> &'a mut Frame {
        assert!(frame_number.0 <= usize::MAX as usize / PAGE_SIZE + 1);
        unsafe { &mut FRAMES[frame_number.0] }
    }

    #[inline(always)]
    pub unsafe fn from_addr<'a>(addr: usize) -> &'a mut Frame {
        let page_num = FrameNumber::from_addr(addr);
        Frame::from_frame_number(page_num)
    }

    pub const fn start_addr(&self) -> usize {
        let frame_number = unsafe {
            (self as *const Frame).offset_from((&raw const FRAMES) as *const Frame) as usize
        };
        frame_number * PAGE_SIZE
    }

    pub fn from_child<'a, T>(child: &'a mut T) -> &'a mut Frame {
        let addr = child as *mut T;
        assert!(addr.addr() >= &raw const FRAMES as usize);
        let max = unsafe { (&raw const FRAMES[PageLevelEntry::MAX]) as usize };
        assert!(addr.addr() < max + size_of::<Frame>());
        unsafe {
            addr.map_addr(|v| v & !(align_of::<Self>() - 1))
                .cast::<Self>()
                .as_mut()
                .unwrap()
        }
    }

    pub fn prev_frame<'a>(&self, count: usize) -> &'a mut Frame {
        assert!(count <= usize::MAX as usize / PAGE_SIZE + 1);
        let addr = unsafe { (self as *const Frame as *mut Frame).sub(count) };
        assert!(addr.addr() >= &raw const FRAMES as usize);
        unsafe { addr.as_mut().unwrap() }
    }

    pub fn next_frame<'a>(&self, count: usize) -> &'a mut Frame {
        assert!(count <= usize::MAX as usize / PAGE_SIZE + 1);
        let addr = unsafe { (self as *const Frame as *mut Frame).add(count) };
        let max = unsafe { (&raw const FRAMES[PageLevelEntry::MAX]) as usize };
        assert!(addr.addr() <= max);
        unsafe { addr.as_mut().unwrap() }
    }
}

pub const fn frame_align_up(value: usize) -> usize {
    (value + PAGE_SIZE - 1) & !(PAGE_SIZE - 1)
}

pub const fn frame_align_down(value: usize) -> usize {
    value & !(PAGE_SIZE - 1)
}

pub const fn frame_count(size: usize) -> usize {
    size.div_ceil(PAGE_SIZE)
}

pub trait FrameAllocator {
    fn allocate<'a>(&self, zone_type: ZoneType, order: FrameOrder) -> Option<&'a mut Frame>;
    fn free(&self, page: &mut Frame) -> Result<(), FrameError>;
    /// 从 buddy 分配器中剔除指定物理内存区域（如 ioremap 需要映射的物理内存）
    fn assign(&self, start: FrameNumber, len: usize) -> Result<(), FrameError>;
}

#[unsafe(export_name = "allocate_frames")]
pub extern "C" fn allocate_frames_c(zone_type: ZoneType, order: FrameOrder) -> usize {
    FRAME_MANAGER
        .allocate(zone_type, order)
        .map_or(0, |v| v.start_addr())
}

#[unsafe(export_name = "free_frames")]
pub extern "C" fn free_frames_c(paddr: usize) -> i8 {
    let frame = unsafe { Frame::from_addr(paddr) };
    match frame.free() {
        Ok(_) => 0,
        Err(_) => -1,
    }
}
