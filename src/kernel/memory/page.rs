use core::ptr::{with_exposed_provenance_mut, NonNull};

use crate::kernel::memory::{block::page_manager, buddy::PageOrder, Page};

pub const PAGE_SIZE: usize = 0x1000;

#[derive(Clone, Copy)]
pub enum PageType {
    Uninited = 0,
    Free,
    HardwareReserved,
    SystemReserved,
    Allocated,
}

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

pub const fn page_count(size: usize) -> u32 {
    (size / PAGE_SIZE) as u32 + (size % PAGE_SIZE != 0) as u32
}

pub const fn page_to_num(addr: usize) -> usize {
    addr / PAGE_SIZE
}

pub trait PageAllocator {
    fn allocate_pages(&mut self, zone_type: ZoneType, order: PageOrder) -> Option<NonNull<Page>>;
    fn free_pages(&mut self, page: NonNull<Page>);
}

#[no_mangle]
pub extern "C" fn allocate_pages(zone_type: ZoneType, order: PageOrder) -> usize {
    unsafe {
        page_manager()
            .unwrap_unchecked()
            .allocate_pages(zone_type, order)
            .map(|v| v.read())
    }
    .map_or(0, |v| v.addr.addr())
}

#[no_mangle]
pub extern "C" fn free_pages(addr: usize) {
    unsafe {
        page_manager()
            .unwrap_unchecked()
            .free_pages(NonNull::new(with_exposed_provenance_mut(addr)).unwrap())
    }
}
