use core::{
    ops::{Add, Sub},
    ptr::{copy_nonoverlapping, with_exposed_provenance_mut, NonNull},
};

use crate::{
    kernel::memory::{
        block::{page_manager, E820Ards, PAGE_INFO_START},
        buddy::{BuddyPage, PageOrder},
        slub::Slub,
    },
    CACHELINE_SIZE,
};

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

pub struct PageHole {
    pub start: PageNumber,
    pub end: PageNumber,
}

pub enum Page {
    End,
    /// 已被管理，但未初始化
    Unused,
    HardwareUsed(PageHole),
    SystemReserved(PageHole),
    Free(PageHole),
    Buddy(BuddyPage),
    Slub(Slub),
}
const _: () = assert!(size_of::<Page>() <= MAX_PAGE_STRUCT_SIZE);

impl Page {
    fn fill_range(start: PageNumber, end: PageNumber, e820_type: u32) {
        let pages: *mut Page = start.get() as *mut Page;
        let count = end.count_from(start);
        let hole = PageHole { start, end };
        let page_info = match e820_type {
            0 => Page::SystemReserved(hole),
            1 => Page::Free(hole),
            _ => Page::HardwareUsed(hole),
        };
        const PAGE_UNINITED: Page = Page::Unused;

        unsafe { pages.write(page_info) };
        for j in 1..count {
            unsafe {
                copy_nonoverlapping(&PAGE_UNINITED as *const Page, pages.add(j), 1);
            }
        }
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
            let block_end = block_start + page_count(block.length as usize - 1);

            if last < block_start {
                // 填充上一个块和当前块之间的空洞为保留
                Self::fill_range(last, block_start - 1, 2);
            }

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

            last = block_end + 1;
        }

        // 最后留一个终止标记
        last = last + 1;
        let page = Page::from_page_number(last);
        unsafe { page.write(Page::End) };

        (last.get() + 1) * size_of::<Page>()
    }
}

impl Page {
    pub const fn from_page_number(page_number: PageNumber) -> *mut Page {
        debug_assert!(page_number.0 <= usize::MAX as usize / PAGE_SIZE);
        unsafe { PAGE_INFO_START.offset(page_number.0 as isize) }
    }

    pub const unsafe fn from_addr(addr: usize) -> *mut Page {
        let page_num = PageNumber::from_addr(addr);
        Page::from_page_number(page_num)
    }

    pub const unsafe fn next_page(&mut self, count: usize) -> *mut Page {
        debug_assert!(count <= usize::MAX as usize / PAGE_SIZE);
        (self as *mut Page).add(count)
    }

    pub const unsafe fn prev_page(&mut self, count: usize) -> *mut Page {
        debug_assert!(count <= usize::MAX as usize / PAGE_SIZE);
        (self as *mut Page).sub(count)
    }

    pub const fn start_addr(&self) -> usize {
        let page_number = unsafe { (self as *const Page).offset_from(PAGE_INFO_START) as usize };
        page_number * PAGE_SIZE
    }
}

/// 内存区域类型
/// Zone只决定了系统能管理的物理内存范围，与Page管理的内存范围无关
#[repr(C)]
#[derive(Clone, Copy)]
pub enum ZoneType {
    DMA = 0,
    #[cfg(any(target_pointer_width = "32", target_pointer_width = "64"))]
    MEM32 = 1,
    #[cfg(target_pointer_width = "64")]
    HighMem = 2,
}

impl ZoneType {
    #[cfg(target_pointer_width = "32")]
    pub const ZONE_COUNT: usize = 2;
    #[cfg(target_pointer_width = "64")]
    pub const ZONE_COUNT: usize = 3;

    pub const fn index(&self) -> usize {
        match self {
            ZoneType::DMA => 0,
            ZoneType::MEM32 => 1,
            #[cfg(target_pointer_width = "64")]
            ZoneType::HighMem => 2,
        }
    }

    pub const fn from_index(index: usize) -> Self {
        match index {
            0 => ZoneType::DMA,
            1 => ZoneType::MEM32,
            #[cfg(target_pointer_width = "64")]
            2 => ZoneType::HighMem,
            _ => panic!("Invalid zone index"),
        }
    }

    pub const fn range(&self) -> (usize, usize) {
        match self {
            ZoneType::DMA => (0, (1 << 24) - 1),             // 16MB
            ZoneType::MEM32 => (1 << 24, u32::MAX as usize), // 4GB
            #[cfg(target_pointer_width = "64")]
            ZoneType::HighMem => (1 << 32, usize::MAX), // >4GB
        }
    }

    pub const fn from_address(addr: usize) -> Self {
        if addr < (1 << 24) {
            ZoneType::DMA
        } else if addr <= u32::MAX as usize {
            ZoneType::MEM32
        } else {
            #[cfg(target_pointer_width = "64")]
            {
                ZoneType::HighMem
            }
            #[cfg(not(target_pointer_width = "64"))]
            {
                unreachable!();
            }
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

#[no_mangle]
pub extern "C" fn allocate_pages(zone_type: ZoneType, order: PageOrder) -> usize {
    unsafe {
        page_manager()
            .unwrap_unchecked()
            .allocate_pages(zone_type, order)
            .map(|v| v.read())
    }
    .map_or(0, |v| v.start_addr())
}

#[no_mangle]
pub extern "C" fn free_pages(addr: usize) -> i8 {
    unsafe {
        match page_manager()
            .unwrap_unchecked()
            .free_pages(NonNull::new(with_exposed_provenance_mut(addr)).unwrap())
        {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }
}
