use core::{
    mem::{ManuallyDrop, zeroed},
    ops::{Add, Deref, DerefMut, Sub},
    pin::Pin,
    ptr::NonNull,
};

use crate::{
    kernel::memory::phy::page::{
        Frame, FrameData, FrameNumber, FrameTag, PAGE_INFO_COUNT, PAGE_SIZE, PageAllocator,
        PageError, ZoneType, page_count,
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
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

#[derive(Clone)]
#[repr(C, align(4))]
pub struct BuddyPage {
    pub order: PageOrder,
    pub zone_type: ZoneType,
}

// impl<'a> From<&'a mut Frame> for &'a mut BuddyPage {
//     fn from(value: &'a mut Frame) -> Self {
//         unsafe { value.data.get_mut().buddy.deref_mut() }
//     }
// }

impl<'a> Into<&'a mut BuddyPage> for &'a mut Frame {
    fn into(self) -> &'a mut BuddyPage {
        unsafe { self.data.get_mut().buddy.deref_mut() }
    }
}

pub struct Zone {
    pub free_pages: [Spinlock<ListHead<Frame>>; MAX_ORDER.0 as usize],
}

pub struct BuddyAllocator {
    pub zones: [Zone; ZoneType::ZONE_COUNT],
}

impl BuddyAllocator {
    pub const fn empty() -> Self {
        unsafe { zeroed() }
    }

    fn get_zone<'a>(&self, zone_type: ZoneType) -> &Zone {
        &self.zones[zone_type.index()]
    }

    /// 初始化Buddy内存分配器
    /// 由于链表需要在目标内存上初始化，所以必须先assume_init，再初始化每一个字段
    pub fn init(&self) {
        let mut type_index = 0;
        let mut zone_type = ZoneType::from_index(type_index);
        let (_, zone_end) = zone_type.range();
        let mut zone_end = FrameNumber::from_addr(zone_end);

        let mut frame = FrameNumber::new(0);

        for i in 0..ZoneType::ZONE_COUNT {
            for free in self.zones[i].free_pages.iter() {
                let mut free = free.lock();
                unsafe { Pin::new_unchecked(free.deref_mut()) }.init();
            }
        }

        let mut current_zone = &self.zones[type_index];
        while frame.get() < PAGE_INFO_COUNT {
            let metadata = Frame::from_frame_number(frame);
            match unsafe { *metadata.tag.get() } {
                FrameTag::Free => {
                    // 只有Free类型的页才会被加入Buddy系统管理
                    let range = unsafe { metadata.data.get_mut().range.deref() };
                    let (mut start, end) = (range.start, range.end);

                    while start <= end {
                        // 找到对应的Zone
                        while zone_end < start {
                            type_index += 1;
                            zone_type = ZoneType::from_index(type_index);
                            current_zone = &self.zones[type_index];
                            let range = zone_type.range();
                            zone_end = FrameNumber::from_addr(range.1);
                        }

                        let temp_end = end
                            .min(start + (MAX_ORDER - 1).to_count() - 1)
                            .min(zone_end);
                        let order = PageOrder::from_page_count(temp_end.count_from(start));

                        unsafe {
                            let frame = Frame::from_frame_number(start);

                            *frame.tag.get_mut() = FrameTag::Buddy;
                            *frame.data.get_mut() = FrameData {
                                buddy: ManuallyDrop::new(BuddyPage { order, zone_type }),
                            };

                            let node = Pin::new_unchecked(frame.list.get_mut());

                            let mut free = current_zone.free_pages[order.val()].lock();
                            let head = Pin::new_unchecked(free.deref_mut());

                            head.add_tail(node);
                        };

                        start = temp_end + 1;
                    }

                    frame = end + 1;
                }
                FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                    // 跳过不可用的页
                    let range = unsafe { (*metadata.data.get()).range.deref() };
                    frame = range.end + 1;
                    continue;
                }
                _ => {
                    panic!("Buddy init error: invalid page type");
                }
            }
        }
    }

    fn split_page(
        &self,
        zone_type: ZoneType,
        order: PageOrder,
        target_order: PageOrder,
        frame: &mut Frame,
    ) {
        let mut split_order = PageOrder::new(order.0 - 1);
        let mut next_page = frame.next_frame(1 << split_order.val());

        while split_order.0 > target_order.0 {
            match unsafe { *next_page.tag.get() } {
                FrameTag::Unused => {
                    *next_page.tag.get_mut() = FrameTag::Buddy;
                    *next_page.data.get_mut() = FrameData {
                        buddy: ManuallyDrop::new(BuddyPage {
                            order: split_order,
                            zone_type,
                        }),
                    };

                    unsafe {
                        let node = Pin::new_unchecked(next_page.list.get_mut());

                        let mut free =
                            self.get_zone(zone_type).free_pages[split_order.val()].lock();
                        let head = Pin::new_unchecked(free.deref_mut());

                        head.add_head(node);
                    }
                }
                _ => {
                    panic!("Split page error: not a buddy page");
                }
            }

            split_order.0 -= 1;
            next_page = frame.next_frame(1 << split_order.val());
        }

        let buddy: &mut BuddyPage = frame.into();
        buddy.order = target_order;
    }

    fn merge_page(&self, left: &mut Frame, right: &mut BuddyPage) {
        let new_order = right.order.0 + 1;
        let buddy: &mut BuddyPage = left.into();
        buddy.order = PageOrder(new_order);

        let page = Frame::from_child(right);
        unsafe {
            *page.tag.get_mut() = FrameTag::Unused;

            let node = Pin::new_unchecked(left.list.get_mut());
            let mut free = self.get_zone(right.zone_type).free_pages[new_order as usize].lock();
            let head = Pin::new_unchecked(free.deref_mut());

            head.add_head(node);
        }
    }
}

impl PageAllocator for BuddyAllocator {
    fn allocate_pages(&self, zone_type: ZoneType, order: PageOrder) -> Option<&mut Frame> {
        let mut order = order;
        let target_order = order;
        let mut page = None;

        while page.is_none() && order < MAX_ORDER {
            let list_head = self.get_zone(zone_type).free_pages[order.val()].lock();

            page = if list_head.is_empty() {
                None
            } else {
                list_first_owner!(Frame, list, list_head)
            };
            if let Some(mut buddy_page) = page {
                let frame = unsafe { buddy_page.as_mut() };

                let list = frame.list.get_mut();
                unsafe { Pin::new_unchecked(list) }.del();

                if order != target_order {
                    self.split_page(zone_type, order, target_order, frame);
                }

                return Some(frame);
            }
            order.0 += 1;
        }
        None
    }

    fn free_pages(&self, page: &mut Frame) -> Result<(), PageError> {
        let addr = page.start_addr();
        let page_number = FrameNumber::from_addr(addr);

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let (zone_start, zone_end) = (
            FrameNumber::from_addr(zone_range.0),
            FrameNumber::new(zone_range.1),
        );

        if let FrameTag::Buddy = unsafe { *page.tag.get() } {
            // 仅释放Buddy类型的页
            let current_order = {
                let this_buddy_page: &mut BuddyPage = page.into();
                this_buddy_page.order
            };

            if current_order < MAX_ORDER - 1 {
                if page_number <= zone_end - current_order.to_count() {
                    let frame = page.next_frame(current_order.to_count());

                    if let FrameTag::Buddy = unsafe { *frame.tag.get() } {
                        self.merge_page(page, frame.into());
                        return Ok(());
                    }
                } else if page_number >= zone_start + current_order.to_count() {
                    let frame = page.prev_frame(current_order.to_count());

                    if let FrameTag::Buddy = unsafe { *frame.tag.get() } {
                        self.merge_page(frame, page.into());
                        return Ok(());
                    }
                } else {
                    panic!("Buddy free error: buddy page out of zone range unexpectedly!");
                }
            }
            if current_order < MAX_ORDER {
                // 无法合并
                unsafe {
                    let node = Pin::new_unchecked(page.list.get_mut());

                    let mut free = zone.free_pages[current_order.val()].lock();
                    let head = Pin::new_unchecked(free.deref_mut());

                    head.add_head(node);
                }
                return Ok(());
            }
        }
        Err(PageError::IncorrectPageType)
    }
}
