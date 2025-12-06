use core::{
    cell::SyncUnsafeCell,
    mem::MaybeUninit,
    ops::{Add, Sub},
    ptr::{NonNull, with_exposed_provenance_mut},
};

use crate::{
    CACHELINE_SIZE,
    kernel::memory::{
        VIR_BASE, VIR_BASE_ADDR,
        phy::{
            E820Ards, PREALLOCATED_END_PHY,
            page::buddy::{BuddyAllocator, BuddyPage, PageOrder},
            slub::Slub,
        },
    },
    lib::rust::spinlock::Spinlock,
};

pub(super) mod buddy;

pub const PAGE_SIZE: usize = 0x1000;

const MAX_PAGE_STRUCT_SIZE: usize = CACHELINE_SIZE;

#[derive(Debug)]
pub enum PageError {
    IncorrectPageType,
}

#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
pub struct PageNumber(usize);

impl PageNumber {
    pub const fn new(num: usize) -> Self {
        PageNumber(num)
    }

    pub const fn from_addr(addr: usize) -> Self {
        PageNumber(addr / PAGE_SIZE)
    }

    pub const fn get(&self) -> usize {
        self.0
    }

    pub const fn count_from(self, other: PageNumber) -> usize {
        if self.0 >= other.0 {
            self.0 - other.0 + 1
        } else {
            other.0 - self.0 + 1
        }
    }
}

impl Add<usize> for PageNumber {
    type Output = PageNumber;

    fn add(self, rhs: usize) -> Self::Output {
        PageNumber(self.0 + rhs)
    }
}

impl Sub<usize> for PageNumber {
    type Output = PageNumber;

    fn sub(self, rhs: usize) -> Self::Output {
        PageNumber(self.0 - rhs)
    }
}

#[repr(C)]
pub struct PageHole {
    pub start: PageNumber,
    pub end: PageNumber,
}

#[cfg(target_pointer_width = "32")]
type PageAlign = [u8; 31];
#[cfg(target_pointer_width = "64")]
type PageAlign = [u8; 63];

#[repr(C, u8)]
pub enum Page {
    /// 暂未使用
    Unused = 0,
    HardwareUsed(PageHole),
    SystemReserved(PageHole),
    Free(PageHole),
    Buddy(BuddyPage),
    Slub(Slub),
    _Align(PageAlign),
}
const _: () = assert!(size_of::<Page>() <= MAX_PAGE_STRUCT_SIZE);

pub const PAGE_INFO_COUNT: usize = 1 << 20;
const PAGE_INFO_SIZE: usize = PAGE_INFO_COUNT * size_of::<Page>();

unsafe extern "C" {
    #[link_name = "page_info"]
    static mut PAGE_INFO: [Page; PAGE_INFO_COUNT];
}

/// Buddy 分配器的虚拟地址存储位置
///
/// 使用SyncUnsafeCell实现内部可变性，由Spinlock真正保证线程安全。
/// 为了简化获取过程，MaybeUninit并不被Spinlock包裹，所以只能在单线程环境下初始化保证安全
static PAGE_MANAGER_VIR: SyncUnsafeCell<MaybeUninit<Spinlock<BuddyAllocator>>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

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

        Page::init(blocks, block_count, (kernel_start, PREALLOCATED_END_PHY));
    }
}

pub fn page_manager<'a>() -> &'a mut Spinlock<BuddyAllocator> {
    unsafe { (*PAGE_MANAGER_VIR.get()).assume_init_mut() }
}

#[unsafe(no_mangle)]
pub extern "C" fn page_init() {
    unsafe {
        let page_manager = PAGE_MANAGER_VIR.get();
        *page_manager = MaybeUninit::new(Spinlock::new(BuddyAllocator::empty()));
        (*page_manager).assume_init_mut().init_with(|pm| pm.init());
    }
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

impl Page {
    /// 填充 [start, end] 范围的Page
    fn fill_range(start: PageNumber, end: PageNumber, e820_type: u32) {
        let pages = Page::from_page_number(start);
        let hole = PageHole { start, end };
        let page_info = match e820_type {
            0 => Page::SystemReserved(hole),
            1 => Page::Free(hole),
            _ => Page::HardwareUsed(hole),
        };

        // 写入首个 page 描述
        unsafe { pages.write(page_info) };
    }

    /// 初始化页结构体数组，根据E820内存块信息划分内存，返回页结构体数组所需的总字节数
    pub fn init(blocks: *mut E820Ards, block_count: u16, kernel_range: (usize, usize)) -> usize {
        let (kernel_start, kernel_end) = (
            PageNumber::from_addr(kernel_range.0),
            PageNumber::from_addr(kernel_range.1),
        );

        let mut last = PageNumber::new(0);
        // 将每个可用内存块按Buddy的方式分割成块
        for i in 0..block_count {
            let block = unsafe { &*blocks.add(i as usize) };

            // 起始地址向后对齐，避免向前越界
            let block_start = PageNumber::from_addr(page_align_up(block.base_addr as usize));
            let block_end = block_start + page_count(block.length as usize);

            if last < block_start {
                // 填充上一个块和当前块之间的空洞为保留
                Self::fill_range(last, block_start - 1, 2);
            }

            if block_start < block_end {
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

            last = block_end;
        }

        PAGE_INFO_SIZE
    }
}

impl Page {
    #[inline(always)]
    pub fn from_page_number(page_number: PageNumber) -> NonNull<Page> {
        debug_assert!(page_number.0 <= usize::MAX as usize / PAGE_SIZE + 1);
        NonNull::from_mut(unsafe { &mut PAGE_INFO[page_number.0] })
    }

    #[inline(always)]
    pub unsafe fn from_addr(addr: usize) -> NonNull<Page> {
        let page_num = PageNumber::from_addr(addr);
        Page::from_page_number(page_num)
    }

    pub const fn next_page(&mut self, count: usize) -> NonNull<Page> {
        debug_assert!(count <= usize::MAX as usize / PAGE_SIZE + 1);
        unsafe { NonNull::from_mut(self).add(count) }
    }

    pub const fn prev_page(&mut self, count: usize) -> NonNull<Page> {
        debug_assert!(count <= usize::MAX as usize / PAGE_SIZE + 1);
        unsafe { NonNull::from_mut(self).sub(count) }
    }

    pub const fn start_addr(&self) -> usize {
        let page_number = unsafe { (self as *const Page).offset_from(&PAGE_INFO[0]) as usize };
        page_number * PAGE_SIZE
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
    fn allocate_pages(&mut self, zone_type: ZoneType, order: PageOrder) -> Option<NonNull<Page>>;
    fn free_pages(&mut self, page: NonNull<Page>) -> Result<(), PageError>;
}

#[unsafe(no_mangle)]
pub extern "C" fn allocate_pages(zone_type: ZoneType, order: PageOrder) -> usize {
    page_manager()
        .lock()
        .allocate_pages(zone_type, order)
        .map_or(0, |v| unsafe { v.as_ref() }.start_addr())
}

#[unsafe(no_mangle)]
pub extern "C" fn free_pages(addr: usize) -> i8 {
    match page_manager()
        .lock()
        .free_pages(NonNull::new(with_exposed_provenance_mut(addr)).unwrap())
    {
        Ok(_) => 0,
        Err(_) => -1,
    }
}
