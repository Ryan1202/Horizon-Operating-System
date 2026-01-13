use core::{
    mem::{ManuallyDrop, zeroed},
    ops::{Add, Deref, DerefMut, Sub},
    pin::Pin,
    ptr::NonNull,
};

use crate::{
    kernel::memory::phy::frame::{
        FRAME_INFO_COUNT, Frame, FrameAllocator, FrameData, FrameError, FrameNumber, FrameTag,
        PAGE_SIZE, ZoneType, frame_count,
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
    list_first_owner,
};

pub const MAX_ORDER: FrameOrder = FrameOrder(11);

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct FrameOrder(u8);

impl Add<u8> for FrameOrder {
    type Output = FrameOrder;

    fn add(self, rhs: u8) -> Self::Output {
        FrameOrder(self.0 + rhs)
    }
}

impl Sub<u8> for FrameOrder {
    type Output = FrameOrder;

    fn sub(self, rhs: u8) -> Self::Output {
        FrameOrder(self.0 - rhs)
    }
}

impl FrameOrder {
    pub const fn new(order: u8) -> Self {
        debug_assert!(order < MAX_ORDER.0);
        FrameOrder(order)
    }

    pub const fn from_frame_count(count: usize) -> Self {
        debug_assert!(count > 0);
        debug_assert!(count <= MAX_ORDER.to_count());
        FrameOrder::new(count.next_power_of_two().ilog2() as u8)
    }

    pub const fn from_size(size: usize) -> Self {
        debug_assert!(size > 0);
        let frame_count = frame_count(size);
        Self::from_frame_count(frame_count)
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
pub struct Buddy {
    pub order: FrameOrder,
    pub zone_type: ZoneType,
}

impl Buddy {
    pub fn from_frame(frame: &mut Frame) -> &mut Self {
        unsafe { frame.get_data_mut().buddy.deref_mut() }
    }
}

pub struct Zone {
    pub free_frames: Spinlock<[ListHead<Frame>; MAX_ORDER.0 as usize]>,
}

/// Zone状态管理，避免重复查询Zone边界
struct ZoneState {
    current_index: usize,
    current_zone_end: FrameNumber,
}

impl ZoneState {
    const fn new() -> Self {
        let zone_type = ZoneType::from_index(0);
        let (_, zone_end) = zone_type.range();
        ZoneState {
            current_index: 0,
            current_zone_end: FrameNumber::from_addr(zone_end),
        }
    }

    /// 获取给定Frame所在的Zone及其边界
    fn get_zone_for_frame(&mut self, frame_number: FrameNumber) -> (ZoneType, FrameNumber) {
        let mut zone_type = ZoneType::from_index(self.current_index);
        let mut zone_end = self.current_zone_end;
        while frame_number > zone_end {
            // 切换到下一个Zone
            self.current_index += 1;
            zone_type = ZoneType::from_index(self.current_index);
            zone_end = FrameNumber::from_addr(zone_type.range().1);
        }
        self.current_zone_end = zone_end;
        (zone_type, zone_end)
    }
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
    /// 1. 初始化所有Zone的空闲链表
    /// 2. 遍历Frame，根据Zone类型和Buddy order分割内存
    pub fn init(&self) {
        // 初始化所有Zone的空闲链表
        self.init_zone_lists();

        // 扫描Frame，初始化Buddy分配器
        self.populate_zones_from_e820();
    }

    /// 初始化所有Zone的空闲链表结构
    fn init_zone_lists(&self) {
        for i in 0..ZoneType::ZONE_COUNT {
            unsafe {
                self.zones[i].free_frames.init_with(|v| {
                    for free in v.iter_mut() {
                        Pin::new_unchecked(free).init();
                    }
                })
            };
        }
    }

    /// 遍历E820内存块，根据Zone和order将Free页加入对应的空闲链表
    fn populate_zones_from_e820(&self) {
        let mut zone_state = ZoneState::new();
        let mut frame_num = FrameNumber::new(0);

        while frame_num.get() < FRAME_INFO_COUNT {
            let frame = Frame::from_frame_number(frame_num);
            match frame.get_tag() {
                FrameTag::Free => {
                    // 获取E820块的范围
                    let block_range = unsafe { frame.get_data_mut().range.deref() };
                    frame_num = block_range.end + 1;
                    self.add_free_block(&mut zone_state, block_range.start, block_range.end);
                }
                FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                    // 跳过保留区
                    let block_range = unsafe { frame.get_data_mut().range.deref() };
                    frame_num = block_range.end + 1;
                }
                FrameTag::Unused => {
                    // 未使用页，可能是跨越了两个不同区域，继续下一个
                    frame_num = frame_num + 1;
                }
                _ => panic!("Buddy init: invalid frame tag"),
            }
        }
    }

    /// 将一个E820内存块按Zone和Order分割加入Buddy系统
    fn add_free_block(&self, zone_state: &mut ZoneState, mut start: FrameNumber, end: FrameNumber) {
        while start <= end {
            // 找到当前地址对应的Zone并获取其范围
            let (zone_type, zone_boundary) = zone_state.get_zone_for_frame(start);
            let zone = self.get_zone(zone_type);

            // 计算在当前Zone内能分割的最大范围
            // 受限于：E820块末尾、Zone边界、最大Order大小
            let chunk_end = end
                .min(start + (MAX_ORDER - 1).to_count() - 1)
                .min(zone_boundary);

            let order = FrameOrder::from_frame_count(chunk_end.count_from(start));

            // 初始化该Buddy块的Frame结构
            unsafe {
                let frame = Frame::from_frame_number(start);

                frame.replace(
                    FrameTag::Buddy,
                    FrameData {
                        buddy: ManuallyDrop::new(Buddy { order, zone_type }),
                    },
                );

                let node = Pin::new_unchecked(frame.list.get_mut());
                let free = &mut zone.free_frames.lock()[order.val()];
                let head = Pin::new_unchecked(free);
                head.add_tail(node);
            }

            start = chunk_end + 1;
        }
    }
}

impl BuddyAllocator {
    fn split(
        &self,
        zone_type: ZoneType,
        order: FrameOrder,
        target_order: FrameOrder,
        frame: &mut Frame,
    ) {
        let mut split_order = FrameOrder::new(order.0 - 1);
        let mut next_frame = frame.next_frame(1 << split_order.val());

        while split_order.0 > target_order.0 {
            match unsafe { *next_frame.tag.get() } {
                FrameTag::Unused => {
                    *next_frame.tag.get_mut() = FrameTag::Buddy;
                    *next_frame.data.get_mut() = FrameData {
                        buddy: ManuallyDrop::new(Buddy {
                            order: split_order,
                            zone_type,
                        }),
                    };

                    unsafe {
                        let node = Pin::new_unchecked(next_frame.list.get_mut());

                        let free =
                            &mut self.get_zone(zone_type).free_frames.lock()[split_order.val()];
                        let head = Pin::new_unchecked(free);

                        head.add_head(node);
                    }
                }
                _ => {
                    panic!("Split frame error: not a buddy frame");
                }
            }

            split_order.0 -= 1;
            next_frame = frame.next_frame(1 << split_order.val());
        }

        let buddy = Buddy::from_frame(frame);
        buddy.order = target_order;
    }

    fn merge(&self, left: &mut Frame, right: &mut Buddy) {
        let new_order = right.order.0 + 1;
        let buddy = Buddy::from_frame(left);
        buddy.order = FrameOrder(new_order);

        let frame = Frame::from_child(right);
        unsafe {
            *frame.tag.get_mut() = FrameTag::Unused;

            let node = Pin::new_unchecked(left.list.get_mut());
            let free = &mut self.get_zone(right.zone_type).free_frames.lock()[new_order as usize];
            let head = Pin::new_unchecked(free);

            head.add_head(node);
        }
    }
}

impl FrameAllocator for BuddyAllocator {
    fn allocate_frames(&self, zone_type: ZoneType, order: FrameOrder) -> Option<&mut Frame> {
        let mut order = order;
        let target_order = order;
        let mut frame = None;

        while frame.is_none() && order < MAX_ORDER {
            let mut guard = self.get_zone(zone_type).free_frames.lock();
            let list_head = &mut guard[order.val()];

            frame = if list_head.is_empty() {
                None
            } else {
                list_first_owner!(Frame, list, list_head)
            };
            if let Some(buddy) = frame {
                let frame = unsafe { buddy.clone().as_mut() };

                let list = frame.list.get_mut();
                unsafe { Pin::new_unchecked(list) }.del();

                drop(guard);

                if order != target_order {
                    self.split(zone_type, order, target_order, frame);
                }

                return Some(frame);
            }
            order.0 += 1;
        }
        None
    }

    fn free_frames(&self, frame: &mut Frame) -> Result<(), FrameError> {
        let addr = frame.start_addr();
        let frame_number = FrameNumber::from_addr(addr);

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let (zone_start, zone_end) = (
            FrameNumber::from_addr(zone_range.0),
            FrameNumber::new(zone_range.1),
        );

        if let FrameTag::Buddy = unsafe { *frame.tag.get() } {
            // 仅释放Buddy类型的页
            let current_order = {
                let this_frame = Buddy::from_frame(frame);
                this_frame.order
            };

            if current_order < MAX_ORDER - 1 {
                if frame_number <= zone_end - current_order.to_count() {
                    let next_frame = frame.next_frame(current_order.to_count());

                    if let FrameTag::Buddy = next_frame.get_tag() {
                        self.merge(frame, Buddy::from_frame(next_frame));
                        return Ok(());
                    }
                } else if frame_number >= zone_start + current_order.to_count() {
                    let prev_frame = frame.prev_frame(current_order.to_count());

                    if let FrameTag::Buddy = prev_frame.get_tag() {
                        self.merge(prev_frame, Buddy::from_frame(frame));
                        return Ok(());
                    }
                } else {
                    panic!("Buddy free error: buddy frame out of zone range unexpectedly!");
                }
            }
            if current_order < MAX_ORDER {
                // 无法合并
                unsafe {
                    let node = Pin::new_unchecked(frame.list.get_mut());

                    let free = &mut zone.free_frames.lock()[current_order.val()];
                    let head = Pin::new_unchecked(free);

                    head.add_head(node);
                }
                return Ok(());
            }
        }
        Err(FrameError::IncorrectFrameType)
    }
}
