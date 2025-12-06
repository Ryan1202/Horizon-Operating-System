use core::{
    ops::{Add, Sub},
    ptr::NonNull,
};

use crate::{
    container_of_enum,
    kernel::memory::phy::page::{
        PAGE_INFO_COUNT, PAGE_SIZE, Page, PageAllocator, PageError, PageNumber, ZoneType,
        page_count,
    },
    lib::rust::list::{ListHead, ListNode},
    list_first_owner,
};

pub const MAX_ORDER: PageOrder = PageOrder(11);

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct PageOrder(u8);

impl Add<u8> for PageOrder {
    type Output = PageOrder;

    fn add(self, rhs: u8) -> Self::Output {
        PageOrder(self.0 + rhs)
    }
}

impl Sub<u8> for PageOrder {
    type Output = PageOrder;

    fn sub(self, rhs: u8) -> Self::Output {
        PageOrder(self.0 - rhs)
    }
}

impl PageOrder {
    pub const fn new(order: u8) -> Self {
        debug_assert!(order < MAX_ORDER.0);
        PageOrder(order)
    }

    pub const fn from_page_count(count: usize) -> Self {
        debug_assert!(count > 0);
        debug_assert!(count <= MAX_ORDER.to_count());
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
        PAGE_SIZE << self.0
    }
}

#[repr(C, align(4))]
pub struct BuddyPage {
    pub list: ListNode<BuddyPage>,
    pub order: PageOrder,
    pub zone_type: ZoneType,
}

#[derive(Clone, Copy)]
pub struct Zone {
    pub free_pages: [ListHead<BuddyPage>; MAX_ORDER.0 as usize],
}

pub struct BuddyAllocator {
    pub zones: [Zone; ZoneType::ZONE_COUNT],
}

impl BuddyAllocator {
    pub const fn empty() -> Self {
        Self {
            zones: [Zone {
                free_pages: [ListHead::empty(); MAX_ORDER.0 as usize],
            }; ZoneType::ZONE_COUNT],
        }
    }

    fn get_zone<'a>(&mut self, zone_type: ZoneType) -> &mut Zone {
        &mut self.zones[zone_type.index()]
    }

    /// 初始化Buddy内存分配器
    /// 由于链表需要在目标内存上初始化，所以必须先assume_init，再初始化每一个字段
    pub fn init(&mut self) {
        let mut type_index = 0;
        let mut zone_type = ZoneType::from_index(type_index);
        let (_, zone_end) = zone_type.range();
        let mut zone_end = PageNumber::from_addr(zone_end);

        let mut page_number = PageNumber::new(0);
        let mut page;

        for i in 0..ZoneType::ZONE_COUNT {
            for list_head in self.zones[i].free_pages.iter_mut() {
                list_head.init();
            }
        }

        let mut current_zone = &mut self.zones[type_index];
        while page_number.get() < PAGE_INFO_COUNT {
            page = Page::from_page_number(page_number);
            match unsafe { page.as_mut() } {
                Page::Free(hole) => {
                    // 只有Free类型的页才会被加入Buddy系统管理
                    let (mut start, end) = (hole.start, hole.end);

                    while start <= end {
                        // 找到对应的Zone
                        while zone_end < start {
                            type_index += 1;
                            zone_type = ZoneType::from_index(type_index);
                            current_zone = &mut self.zones[type_index];
                            let range = zone_type.range();
                            zone_end = PageNumber::from_addr(range.1);
                        }

                        let temp_end = end
                            .min(start + (MAX_ORDER - 1).to_count() - 1)
                            .min(zone_end);
                        let order = PageOrder::from_page_count(temp_end.count_from(start));

                        unsafe {
                            let mut page = Page::from_page_number(start);
                            page.write(Page::Buddy(BuddyPage {
                                list: ListNode::new(),
                                order,
                                zone_type,
                            }));

                            let page = page.as_mut();
                            if let Page::Buddy(buddy_page) = page {
                                current_zone.free_pages[order.val()].add_tail(&mut buddy_page.list);
                            }
                        };

                        start = temp_end + 1;
                    }

                    page_number = end + 1;
                }
                Page::HardwareUsed(hole) | Page::SystemReserved(hole) => {
                    // 跳过不可用的页
                    page_number = hole.end + 1;
                    continue;
                }
                _ => {
                    panic!("Buddy init error: invalid page type");
                }
            }
        }
    }

    fn split_page(
        &mut self,
        zone_type: ZoneType,
        order: PageOrder,
        target_order: PageOrder,
        page: &mut Page,
    ) {
        let mut split_order = PageOrder::new(order.0 - 1);
        let mut next_page = unsafe { page.next_page(1 << split_order.val()).as_mut() };

        while split_order.0 > target_order.0 {
            match next_page {
                Page::Unused => {
                    *next_page = Page::Buddy(BuddyPage {
                        list: ListNode::new(),
                        order: split_order,
                        zone_type,
                    });

                    if let Page::Buddy(buddy_page) = next_page {
                        self.get_zone(zone_type).free_pages[split_order.val()]
                            .add_head(&mut buddy_page.list);
                    }
                }
                _ => {
                    panic!("Split page error: not a buddy page");
                }
            }

            split_order.0 -= 1;
            next_page = unsafe { page.next_page(1 << split_order.val()).as_mut() };
        }

        if let Page::Buddy(buddy_page) = page {
            buddy_page.order = target_order;
        } else {
            panic!("Split page error: not a buddy page");
        }
    }

    fn merge_page(&mut self, left: &mut BuddyPage, right: &mut BuddyPage) {
        left.order = PageOrder::new(right.order.0 + 1);

        let page = container_of_enum!(NonNull::from_mut(left), Page, Buddy.0);
        unsafe {
            page.write(Page::Unused);

            self.get_zone(right.zone_type).free_pages[left.order.val()].add_head(&mut left.list);
        }
    }
}

impl PageAllocator for BuddyAllocator {
    fn allocate_pages(&mut self, zone_type: ZoneType, order: PageOrder) -> Option<NonNull<Page>> {
        let mut order = order;
        let target_order = order;
        let mut page = None;

        while page.is_none() && order < MAX_ORDER {
            let list_head = &mut self.get_zone(zone_type).free_pages[order.val()];

            page = if list_head.is_empty() {
                None
            } else {
                list_first_owner!(BuddyPage, list, list_head)
            };
            if let Some(mut buddy_page) = page {
                let _buddy_page = unsafe { buddy_page.as_mut() };
                _buddy_page.list.del();

                let mut page = container_of_enum!(buddy_page, Page, Buddy.0);
                let page = unsafe { page.as_mut() };

                if order != target_order {
                    self.split_page(zone_type, order, target_order, page);
                }

                return Some(NonNull::from_mut(page));
            }
            order.0 += 1;
        }
        None
    }

    fn free_pages(&mut self, mut page: NonNull<Page>) -> Result<(), PageError> {
        let _page = unsafe { page.as_mut() };
        let addr = _page.start_addr();
        let page_number = PageNumber::from_addr(addr);

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let (zone_start, zone_end) = (
            PageNumber::from_addr(zone_range.0),
            PageNumber::new(zone_range.1),
        );

        if let Page::Buddy(this_buddy_page) = _page {
            // 仅释放Buddy类型的页
            let current_order = this_buddy_page.order;
            if current_order < MAX_ORDER {
                if page_number <= zone_end - current_order.to_count() {
                    let mut buddy_page =
                        unsafe { page.as_mut() }.next_page(current_order.to_count());

                    unsafe {
                        if let Page::Buddy(buddy_page) = buddy_page.as_mut() {
                            self.merge_page(this_buddy_page, buddy_page);
                            return Ok(());
                        }
                    }
                } else if page_number >= zone_start + current_order.to_count() {
                    let mut buddy_page =
                        unsafe { page.as_mut() }.prev_page(current_order.to_count());

                    unsafe {
                        if let Page::Buddy(buddy_page) = buddy_page.as_mut() {
                            self.merge_page(buddy_page, this_buddy_page);
                            return Ok(());
                        }
                    }
                } else {
                    panic!("Buddy free error: buddy page out of zone range unexpectedly!");
                }
            }
            // 无法合并，直接加入空闲链表
            zone.free_pages[current_order.val()].add_head(&mut this_buddy_page.list);

            Ok(())
        } else {
            Err(PageError::IncorrectPageType)
        }
    }
}
