use core::{
    cell::SyncUnsafeCell,
    mem::{self, ManuallyDrop},
    ops::{Add, Sub},
    ptr::NonNull,
};

use crate::{
    CACHELINE_SIZE,
    arch::x86::kernel::page::PageLevelEntry,
    kernel::memory::{
        VIR_BASE, VIR_BASE_ADDR,
        page::PageTableEntry,
        phy::{
            E820Ards, PREALLOCATED_END_PHY,
            page::buddy::{BuddyAllocator, BuddyPage, PageOrder},
            slub::Slub,
        },
    },
    lib::rust::list::ListNode,
};

pub mod buddy;

pub const PAGE_SIZE: usize = 0x1000;

#[cfg(target_pointer_width = "32")]
const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE / 2;
#[cfg(target_pointer_width = "64")]
const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE;

#[derive(Debug)]
pub enum PageError {
    IncorrectPageType,
}

#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
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
    // list 在前，8字节对齐
    pub(super) list: SyncUnsafeCell<ListNode<Frame>>,
    // 其他字段在后
    pub data: SyncUnsafeCell<FrameData>,
    pub tag: SyncUnsafeCell<FrameTag>,
}

unsafe impl Sync for Frame {}

pub union FrameData {
    pub unused: (),
    pub range: ManuallyDrop<FrameRange>,
    pub buddy: ManuallyDrop<BuddyPage>,
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
    Free = 3,
    Buddy = 4,
    Slub = 5,
    Io = 6,
    Vmalloc = 7,
}

const _: () = assert!(size_of::<Frame>() <= MAX_PAGE_STRUCT_SIZE);

pub const PAGE_INFO_COUNT: usize = 1 << 20;
const PAGE_INFO_SIZE: usize = PAGE_INFO_COUNT * size_of::<Frame>();

unsafe extern "C" {
    // #[link_name = "page_info"]
    // static mut PAGE_INFO: [Frame; PAGE_INFO_COUNT];
}

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
        // 只是为了看着稍微舒服一点
        PREALLOCATED_END_PHY = page_align_up(PREALLOCATED_END_PHY.max(kernel_end));

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
        *frame.data.get_mut() = data;
        *frame.tag.get_mut() = tag;
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
            let block_start = FrameNumber::from_addr(page_align_up(block.base_addr as usize));
            let block_end = block_start + page_count(block.length as usize);

            if last < block_start {
                // 填充上一个块和当前块之间的空洞为保留
                Self::fill_range(last, block_start - 1, 2);
            }
            last = block_end;

            if block_start >= block_end {
                continue;
            }

            // 填充 [block_start, block_end) 范围
            if block_end <= kernel_start || block_start >= kernel_end {
                Self::fill_range(block_start, block_end - 1, block.block_type);
            } else {
                // 内存块和内核有重叠部分
                if block_start < kernel_start {
                    // 前半部分可用
                    Self::fill_range(block_start, kernel_start - 1, block.block_type);
                }
                Self::fill_range(kernel_start, kernel_end, 0);
                if block_end > kernel_end {
                    // 后半部分可用
                    Self::fill_range(kernel_end + 1, block_end - 1, block.block_type);
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

/// 内存区域类型
/// Zone只决定了系统能管理的物理内存范围，与Page管理的内存范围无关
#[repr(u8)]
#[derive(Clone, Copy, Debug)]
pub enum ZoneType {
    /// (ISA )DMA区，低于16MB
    MEM24 = 0,
    /// 内核线性映射区，低于4GB（64位）或512MB（32位）
    LinearMem = 1,
    /// 高端内存区，超过4GB（64位）或512MB（32位）
    HighMem = 2,
}

impl ZoneType {
    pub const ZONE_COUNT: usize = 3;

    pub const fn index(&self) -> usize {
        match self {
            ZoneType::MEM24 => 0,
            ZoneType::LinearMem => 1,
            ZoneType::HighMem => 2,
        }
    }

    pub const fn from_index(index: usize) -> Self {
        match index {
            0 => ZoneType::MEM24,
            1 => ZoneType::LinearMem,
            2 => ZoneType::HighMem,
            _ => panic!("Invalid zone index"),
        }
    }

    pub const fn range(&self) -> (usize, usize) {
        match self {
            ZoneType::MEM24 => (0, (1 << 24) - 1), // 16MB
            #[cfg(target_pointer_width = "32")]
            ZoneType::LinearMem => (1 << 24, 0x1fffffff), // 512MB
            #[cfg(target_pointer_width = "64")]
            ZoneType::LinearMem => (1 << 24, 1 << 32), // 4GB
            #[cfg(target_pointer_width = "32")]
            ZoneType::HighMem => (0x20000000, usize::MAX), // >512MB
            #[cfg(target_pointer_width = "64")]
            ZoneType::HighMem => (1 << 32, usize::MAX), // >4GB
        }
    }

    pub const fn from_address(addr: usize) -> Self {
        match (addr | 1).ilog2() {
            0..24 => ZoneType::MEM24,
            #[cfg(target_pointer_width = "32")]
            24..29 => ZoneType::LinearMem,
            #[cfg(target_pointer_width = "64")]
            24..32 => ZoneType::LinearMem,
            _ => ZoneType::HighMem,
        }
    }
}

pub const fn page_align_up(value: usize) -> usize {
    (value + PAGE_SIZE - 1) & !(PAGE_SIZE - 1)
}

pub const fn page_align_down(value: usize) -> usize {
    value & !(PAGE_SIZE - 1)
}

pub const fn page_count(size: usize) -> usize {
    (size / PAGE_SIZE) + (size % PAGE_SIZE != 0) as usize
}

pub trait PageAllocator {
    fn allocate_pages(&self, zone_type: ZoneType, order: PageOrder) -> Option<&mut Frame>;
    fn free_pages(&self, page: &mut Frame) -> Result<(), PageError>;
}

#[unsafe(export_name = "allocate_pages")]
pub extern "C" fn allocate_pages_c(zone_type: ZoneType, order: PageOrder) -> usize {
    FRAME_MANAGER
        .allocate_pages(zone_type, order)
        .map_or(0, |v| v.start_addr())
}

#[unsafe(export_name = "free_pages")]
pub extern "C" fn free_pages_c(paddr: usize) -> i8 {
    let page = unsafe { Frame::from_addr(paddr) };
    match FRAME_MANAGER.free_pages(page) {
        Ok(_) => 0,
        Err(_) => -1,
    }
}
