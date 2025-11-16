use core::{
    ffi::c_void,
    ptr::{null_mut, NonNull},
};

use crate::{
    kernel::memory::{
        block::{E820Ards, PAGE_INFO_START, PREALLOCATED_END_PHY},
        page::{
            page_align_down, page_align_up, page_count, page_to_num, PageAllocator, PageType,
            ZoneType, PAGE_SIZE,
        },
        slub::Slub,
    },
    lib::rust::list::{ListHead, ListNode},
    list_first_owner, list_owner,
};

const MAX_ORDER: u8 = 11;

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct PageOrder(u8);

impl PageOrder {
    pub const fn new(order: u8) -> Self {
        debug_assert!(order <= MAX_ORDER as u8);
        PageOrder(order)
    }

    pub const fn from_page_count(count: u32) -> Self {
        debug_assert!(count > 0);
        debug_assert!(count <= (1 << MAX_ORDER));
        PageOrder::new(count.next_power_of_two().ilog2() as u8)
    }

    pub const fn from_size(size: usize) -> Self {
        debug_assert!(size > 0);
        let page_count = page_count(size);
        Self::from_page_count(page_count)
    }

    pub const fn val(&self) -> usize {
        self.0 as usize
    }

    pub const fn to_count(&self) -> usize {
        1 << self.0
    }

    pub const fn to_size(&self) -> usize {
        0x1000 << self.0
    }
}
#[repr(C)]
pub struct Page {
    pub list: ListNode<Page>,
    pub addr: *mut c_void,
    pub order: PageOrder,
    pub head: *mut Page,
    pub page_type: PageType,
    pub slub: *mut Slub,
}

impl Page {
    pub const fn from_page_num(page_num: usize) -> *mut Page {
        debug_assert!(page_num <= usize::MAX as usize / PAGE_SIZE);
        unsafe { PAGE_INFO_START.offset(page_num as isize) }
    }

    pub const unsafe fn from_addr(addr: usize) -> *mut Page {
        let page_num = addr / PAGE_SIZE;
        Page::from_page_num(page_num)
    }

    pub const unsafe fn next_page(&mut self, count: usize) -> *mut Page {
        debug_assert!(count <= usize::MAX as usize / PAGE_SIZE);
        (self as *mut Page).add(count)
    }

    pub const unsafe fn prev_page(&mut self, count: usize) -> *mut Page {
        debug_assert!(count <= usize::MAX as usize / PAGE_SIZE);
        (self as *mut Page).sub(count)
    }
}

#[derive(Clone, Copy)]
pub struct Zone {
    pub allocated_pages: ListHead<Page>,
    pub free_pages: [ListHead<Page>; (MAX_ORDER + 1) as usize],
}

pub struct BuddyAllocator {
    pub zones: [Zone; ZoneType::ZONE_COUNT],
}

impl BuddyAllocator {
    pub fn early_init(&mut self, blocks: *mut E820Ards, block_count: u16) {
        let zones = &mut self.zones;
        for i in 0..ZoneType::ZONE_COUNT {
            zones[i].allocated_pages.init();
            for order in 0..=MAX_ORDER as usize {
                zones[i].free_pages[order].init();
            }
        }

        // 将每个可用内存块按Buddy的方式分割成块
        for i in 0..block_count {
            let block = unsafe { &*blocks.add(i as usize) };
            let mut block_start = block.base_addr as usize;
            let block_end = block_start + ((block.length - 1) as usize);

            // 起始地址向后对齐，避免向前越界
            block_start = page_align_up(block_start);
            // 结束地址向前对齐，避免越界
            let block_end = page_align_down(block_end);

            let page_type = match block.block_type {
                1 => PageType::Free,
                _ => PageType::HardwareReserved,
            };

            let start = page_to_num(block_start);
            let end = page_to_num(block_end);
            for page_num in start..end {
                let page = unsafe { Page::from_page_num(page_num).as_mut().unwrap_unchecked() };
                let addr = (page_num << 12) as *mut c_void;
                page.addr = addr;
                page.head = null_mut();
                page.page_type = page_type;
                page.list.init();
            }
        }
    }

    pub fn remark_page(&mut self, page_addr: usize, page_type: PageType) {
        let page = unsafe { Page::from_addr(page_addr).as_mut().unwrap_unchecked() };
        page.page_type = page_type;
    }

    const fn get_zone<'a>(&'a mut self, zone_type: ZoneType) -> &'a mut Zone {
        &mut self.zones[zone_type.index()]
    }

    pub fn init(&mut self, blocks: *mut E820Ards, block_count: u16, kernel_start: usize) {
        let mut type_index = 0;
        let mut current_zone = &mut self.zones[type_index];
        let mut zone_type = ZoneType::from_index(type_index);
        let (mut zone_start, mut zone_end) = zone_type.range();

        let mut block_index = 0;
        let mut block = unsafe { &*blocks.add(block_index) };
        let mut block_start = page_align_up(block.base_addr as usize);
        let mut block_end = page_align_down((block.base_addr + block.length) as usize);

        let mut address = 0usize;
        let mut end;
        'outer: loop {
            if address < kernel_start {
                end = zone_end
                    .min(block_end as usize)
                    .min(kernel_start)
                    .min(address + (1 << MAX_ORDER) * PAGE_SIZE);
            } else if address < unsafe { PREALLOCATED_END_PHY } {
                address = unsafe { PREALLOCATED_END_PHY };
                while zone_end <= address {
                    if type_index >= ZoneType::ZONE_COUNT {
                        break 'outer;
                    }
                    type_index += 1;
                    zone_type = ZoneType::from_index(type_index);
                    current_zone = self.get_zone(zone_type);
                    (zone_start, zone_end) = zone_type.range();
                }
                while block_end as usize <= address {
                    block_index += 1;
                    if block_index >= block_count as usize {
                        break 'outer;
                    }
                    block = unsafe { &*blocks.add(block_index) };
                    block_start = page_align_up(block.base_addr as usize);
                    block_end = page_align_down((block.base_addr + block.length) as usize);
                }
                address = address.max(block_start as usize).max(zone_start);
                end = zone_end
                    .min(block_end as usize)
                    .min(address + (1 << MAX_ORDER) * PAGE_SIZE);
            } else {
                end = zone_end
                    .min(block_end as usize)
                    .min(address + (1 << MAX_ORDER) * PAGE_SIZE);
            }
            let mut count = page_count(end - address);
            let mut page = unsafe { Page::from_addr(address).as_mut().unwrap_unchecked() };
            let page_type = if block.block_type == 1 {
                PageType::Free
            } else {
                PageType::HardwareReserved
            };
            while count > 0 {
                let order = count.ilog2() as u8;
                let head = page as *mut Page;
                if let PageType::Free = page_type {
                    unsafe { current_zone.free_pages[order as usize].add_tail(&mut page.list) };
                }
                page.head = head;
                page.order = PageOrder::new(order);
                page.page_type = page_type;

                // let block_count = 1 << order;
                // for i in 1..block_count {
                //     let next = unsafe { page.next_page(i).as_mut().unwrap_unchecked() };
                //     next.head = head;
                //     next.order = PageOrder::new(order);
                //     next.list.init();
                //     next.page_type = page_type;
                // }
                count -= 1 << order;
                page = unsafe { page.next_page(1).as_mut() }.unwrap();
            }

            while zone_end <= end {
                if type_index >= ZoneType::ZONE_COUNT {
                    break 'outer;
                }
                type_index += 1;
                zone_type = ZoneType::from_index(type_index);
                current_zone = self.get_zone(zone_type);
                (zone_start, zone_end) = zone_type.range();
            }
            while block_end as usize <= end {
                block_index += 1;
                if block_index >= block_count as usize {
                    break 'outer;
                }
                block = unsafe { &*blocks.add(block_index) };
                block_start = page_align_up(block.base_addr as usize);
                block_end = page_align_down((block.base_addr + block.length) as usize);
            }
            address = end.max(zone_start).max(block_start as usize);
        }
    }

    /// 返回 BuddyAllocator 自身 + 所有 Page 结构体占用的总内存字节数。
    pub const fn total_footprint() -> usize {
        let buddy_size = core::mem::size_of::<BuddyAllocator>();

        let mut max_page_count = 0;
        let mut i = 0;
        while i < ZoneType::ZONE_COUNT {
            let zone_type = ZoneType::from_index(i);
            let (_, end) = zone_type.range();
            let page_count = page_count(end);
            if page_count > max_page_count {
                max_page_count = page_count;
            }
            i += 1;
        }
        let pages_size = core::mem::size_of::<Page>() * max_page_count as usize;
        buddy_size + pages_size
    }

    fn split_page(
        &mut self,
        zone_type: ZoneType,
        order: PageOrder,
        target_order: PageOrder,
        page: &mut Page,
    ) {
        let mut split_order = PageOrder::new(order.0 - 1);
        let mut buddy_page = unsafe {
            page.next_page(1 << split_order.val())
                .as_mut()
                .unwrap_unchecked()
        };
        while split_order.0 > target_order.0 {
            unsafe {
                self.get_zone(zone_type).free_pages[split_order.val()]
                    .add_head(&mut buddy_page.list);
            }
            buddy_page.order = split_order;
            buddy_page.head = buddy_page;
            buddy_page = unsafe {
                page.next_page(1 << (split_order.val() - 1))
                    .as_mut()
                    .unwrap_unchecked()
            };
            split_order.0 -= 1;
        }
        page.order = target_order;
    }
}

impl PageAllocator for BuddyAllocator {
    fn allocate_pages(&mut self, zone_type: ZoneType, order: PageOrder) -> Option<NonNull<Page>> {
        let mut order = order;
        let target_order = order;
        let mut page = None;

        while page.is_none() {
            let list_head = self.get_zone(zone_type).free_pages[order.val()];

            page = if list_head.is_empty() {
                None
            } else {
                list_first_owner!(Page, list, list_head)
            };
            if let Some(page) = page {
                let page = unsafe { page.as_mut().unwrap_unchecked() };
                unsafe { page.list.del() };

                if order != target_order {
                    self.split_page(zone_type, order, target_order, page);
                }

                unsafe {
                    self.get_zone(zone_type)
                        .allocated_pages
                        .add_tail(&mut page.list)
                };
                return Some(unsafe { NonNull::new_unchecked(page) });
            }
            order.0 += 1;
        }
        None
    }

    fn free_pages(&mut self, mut page: NonNull<Page>) {
        let page = unsafe { page.as_mut() };
        let addr = page.addr.addr();

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let current_order = page.order;

        unsafe { page.list.del() };

        let page_num = page_to_num(addr);
        if (page.order.val() as u8) < MAX_ORDER {
            if page_num <= page_to_num(zone_range.1) - current_order.to_count() {
                let buddy_page =
                    unsafe { page.next_page(current_order.to_count()).as_mut().unwrap() };
                if buddy_page.head == (buddy_page as *mut Page) && buddy_page.order == current_order
                {
                    unsafe { buddy_page.list.del() };
                    page.order = PageOrder::new(current_order.0 + 1);
                    buddy_page.order = page.order;
                    buddy_page.head = page as *mut Page;

                    unsafe { zone.free_pages[page.order.val()].add_head(&mut page.list) };
                    return;
                }
            } else if page_num >= page_to_num(zone_range.0) + current_order.to_count() {
                let buddy_page =
                    unsafe { page.prev_page(current_order.to_count()).as_mut().unwrap() };
                if buddy_page.head == page.head && buddy_page.order == current_order {
                    unsafe { buddy_page.list.del() };
                    page.order = PageOrder::new(current_order.0 + 1);
                    buddy_page.order = page.order;
                    page.head = buddy_page as *mut Page;

                    unsafe { zone.free_pages[page.order.val()].add_head(&mut buddy_page.list) };
                    return;
                }
            }
        }

        // 无法合并，直接加入空闲链表
        unsafe { zone.free_pages[page.order.val()].add_head(&mut page.list) };
    }
}
