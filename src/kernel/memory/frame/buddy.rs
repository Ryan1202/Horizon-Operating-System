use core::{
    cell::SyncUnsafeCell,
    mem::{self, ManuallyDrop, offset_of, zeroed},
    num::NonZeroUsize,
    ops::{Add, DerefMut, Sub},
    pin::Pin,
    ptr::NonNull,
    sync::atomic::Ordering,
};

use crate::{
    arch::ArchPageTable,
    kernel::memory::{
        arch::ArchMemory,
        frame::{
            ALLOCATED_PAGES, FRAME_INFO_COUNT, Frame, FrameAllocator, FrameData, FrameError,
            FrameNumber, FrameTag,
            anonymous::Anonymous,
            frame_count,
            reference::UniqueFrames,
            zone::{RESERVED_END, ZONE_COUNT, ZoneType},
        },
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
};

pub const MAX_ORDER: FrameOrder = FrameOrder(10);

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
        debug_assert!(order <= MAX_ORDER.0);
        FrameOrder(order)
    }

    pub const fn from_count(count: usize) -> Self {
        debug_assert!(count > 0);
        debug_assert!(count < MAX_ORDER.to_count().get() * 2);
        FrameOrder::new(count.next_power_of_two().ilog2() as u8)
    }

    pub const fn from_size(size: usize) -> Self {
        debug_assert!(size > 0);
        let frame_count = frame_count(size);
        Self::from_count(frame_count)
    }

    pub const fn get(&self) -> usize {
        self.0 as usize
    }

    pub const fn to_count(&self) -> NonZeroUsize {
        unsafe { NonZeroUsize::new_unchecked(1 << self.0) }
    }

    pub const fn to_size(&self) -> usize {
        ArchPageTable::PAGE_SIZE << self.0
    }
}

#[repr(C, align(4))]
pub struct Buddy {
    list: SyncUnsafeCell<ListNode<Buddy>>,
    pub order: FrameOrder,
    pub zone_type: ZoneType,
}

unsafe impl Sync for Buddy {}

impl Buddy {
    pub const fn new(order: FrameOrder, zone_type: ZoneType) -> Self {
        Self {
            list: SyncUnsafeCell::new(ListNode::new()),
            order,
            zone_type,
        }
    }

    const fn list_offset() -> usize {
        offset_of!(Buddy, list)
    }

    fn get_list(&mut self) -> Pin<&mut ListNode<Buddy>> {
        let list = self.list.get_mut();
        unsafe { Pin::new_unchecked(list) }
    }
}

impl<'a> TryFrom<&'a mut Frame> for &'a mut Buddy {
    type Error = FrameError;

    fn try_from(value: &'a mut Frame) -> Result<Self, Self::Error> {
        match value.get_tag() {
            FrameTag::Buddy => Ok(unsafe { value.get_data_mut().buddy.deref_mut() }),
            _ => Err(FrameError::IncorrectFrameType),
        }
    }
}

pub struct Zone {
    pub free_frames: Spinlock<[ListHead<Buddy>; MAX_ORDER.0 as usize + 1]>,
}

/// Zone状态管理，避免重复查询Zone边界
struct ZoneState {
    current_index: usize,
    zone_start: FrameNumber,
    zone_end: FrameNumber,
}

impl ZoneState {
    const fn new() -> Self {
        let zone_type = ZoneType::from_index(0);
        let (zone_start, zone_end) = zone_type.range();
        ZoneState {
            current_index: 0,
            zone_start: zone_start.to_frame_number(),
            zone_end: zone_end.to_frame_number(),
        }
    }

    /// 获取给定Frame所在的Zone及其边界
    fn get_zone_for_frame(
        &mut self,
        frame_number: FrameNumber,
    ) -> (ZoneType, FrameNumber, FrameNumber) {
        let mut zone_type = ZoneType::from_index(self.current_index);
        let mut zone_start = self.zone_start;
        let mut zone_end = self.zone_end;
        while frame_number >= zone_end {
            // 切换到下一个Zone
            self.current_index += 1;
            zone_type = ZoneType::from_index(self.current_index);

            let range = zone_type.range();

            zone_start = range.0.to_frame_number();
            zone_end = range.1.to_frame_number();
        }
        self.zone_start = zone_start;
        self.zone_end = zone_end;
        (zone_type, zone_start, zone_end)
    }
}

pub struct BuddyAllocator {
    pub zones: [Zone; ZONE_COUNT],
}

impl BuddyAllocator {
    pub const fn empty() -> Self {
        unsafe { zeroed() }
    }

    fn get_zone(&self, zone_type: ZoneType) -> &Zone {
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
        for i in 0..ZONE_COUNT {
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
        let mut frame_number = FrameNumber::new(0);

        while frame_number.get() < FRAME_INFO_COUNT {
            // 所有 Frame 默认初始化为独占 Free, 所以需要使用 `Frame::get_unique` 来获取
            let frame = Frame::get_raw(frame_number);
            match Frame::get_tag_relaxed(frame_number) {
                FrameTag::Free => {
                    // 获取E820块的范围
                    let frame = UniqueFrames::from_allocator(frame, FrameOrder(0), self).unwrap();

                    let block_range = unsafe { frame.get_data().range };
                    frame_number = block_range.end;

                    self.add_free_block(&mut zone_state, block_range.start, block_range.end);
                }
                FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                    // 跳过保留区
                    let range = unsafe { frame.as_ref().get_data().range };

                    frame_number = range.end;
                }
                FrameTag::Uninited => {
                    // 不可用页，可能是跨越了两个不同区域，继续下一个
                    frame_number = frame_number + 1;
                }
                _ => panic!("Buddy init: invalid frame tag"),
            }
        }
    }

    /// 将一个E820内存块按Zone和Order分割加入Buddy系统
    fn add_free_block(&self, zone_state: &mut ZoneState, mut start: FrameNumber, end: FrameNumber) {
        while start < end {
            // 找到当前地址对应的Zone并获取其范围
            let (zone_type, zone_start, zone_end) = zone_state.get_zone_for_frame(start);
            let zone = self.get_zone(zone_type);
            start = start.max(zone_start);

            // 起始地址也要对齐到当前Order
            let max_order = if start.get() == 0 {
                MAX_ORDER
            } else {
                FrameOrder::new((start.get().trailing_zeros() as u8).min(MAX_ORDER.0))
            };

            // 计算在当前Zone内能分割的最大范围
            // 受限于：E820块末尾、Zone边界、最大Order大小
            let chunk_end = end.min(start + max_order.to_count().get()).min(zone_end);

            let order = FrameOrder::from_count(chunk_end.count_from(start));

            // 初始化该Buddy块的Frame结构
            unsafe {
                let mut frame =
                    UniqueFrames::from_allocator(Frame::get_raw(start), FrameOrder(0), self)
                        .unwrap();

                frame.replace(
                    FrameTag::Buddy,
                    FrameData {
                        buddy: ManuallyDrop::new(Buddy {
                            list: SyncUnsafeCell::new(ListNode::new()),
                            order,
                            zone_type,
                        }),
                    },
                );

                let _ = self.add_to_free_list((*frame).deref_mut(), order, zone);
            }

            start = chunk_end;
        }
    }
}

impl BuddyAllocator {
    fn split(
        &self,
        zone_type: ZoneType,
        order: FrameOrder,
        target_order: FrameOrder,
        frame: &mut UniqueFrames,
    ) {
        let mut split_order = FrameOrder::new(order.0 - 1);
        let mut next_frame = frame.split();

        while split_order.0 > target_order.0 {
            match next_frame.get_tag() {
                FrameTag::Buddy => {
                    panic!("Split error: frame is already a buddy block");
                }
                FrameTag::Tail | FrameTag::Uninited => {}
                _ => {
                    panic!("Split error: frame is not a buddy block");
                }
            }

            let buddy = ManuallyDrop::new(Buddy {
                list: SyncUnsafeCell::new(ListNode::new()),
                order: split_order,
                zone_type,
            });

            unsafe {
                next_frame.replace(FrameTag::Buddy, FrameData { buddy });

                let node = Buddy::get_list((*next_frame).deref_mut().try_into().unwrap());

                let free = &mut self.get_zone(zone_type).free_frames.lock()[split_order.get()];
                let mut head = Pin::new_unchecked(free);

                head.add_tail(node);

                mem::forget(next_frame);
            }

            split_order.0 -= 1;
            next_frame = frame.split();
        }

        let buddy: &mut Buddy = frame.deref_mut().try_into().unwrap();
        buddy.order = target_order;
    }

    fn merge_exact(&self, left: &mut UniqueFrames, right: ManuallyDrop<UniqueFrames>) {
        let buddy: &mut Buddy = left.deref_mut().try_into().unwrap();

        let new_order = buddy.order.0 + 1;
        buddy.order = FrameOrder(new_order);

        let zone_type = buddy.zone_type;

        let mut frame = right;
        unsafe {
            frame.set_tag(FrameTag::Uninited);

            let node = Buddy::get_list(left.deref_mut().try_into().unwrap());
            let free = &mut self.get_zone(zone_type).free_frames.lock()[new_order as usize];
            let mut head = Pin::new_unchecked(free);

            head.add_head(node);
        }
    }

    fn merge_once(
        &self,
        frame: ManuallyDrop<UniqueFrames>,
        current_order: FrameOrder,
        range: (FrameNumber, FrameNumber),
    ) -> Result<ManuallyDrop<UniqueFrames>, ManuallyDrop<UniqueFrames>> {
        let frame_number = frame.to_frame_number();

        let new_order = current_order + 1;
        let count = new_order.to_count().get();

        let low = frame_number.align_down(new_order);
        let high = low + count;

        if low < range.0 || high > range.1 {
            return Err(frame);
        }

        let is_low = low == frame_number;
        let buddy = Frame::get_raw(if is_low { high } else { low });

        let pair = match UniqueFrames::from_allocator(buddy, current_order, self) {
            Some(buddy) => {
                if is_low {
                    Ok((frame, buddy))
                } else {
                    Ok((buddy, frame))
                }
            }
            None => Err(frame),
        };

        pair.and_then(|(low, high)| {
            UniqueFrames::merge(low, high, self, Self::merge_exact)
                .map_err(|(low, high)| if is_low { low } else { high })
        })
    }

    /// 将 Frame 添加回空闲链表
    fn add_to_free_list(
        &self,
        frame: &mut Frame,
        order: FrameOrder,
        zone: &Zone,
    ) -> Result<(), FrameError> {
        unsafe {
            let node = Buddy::get_list(frame.try_into()?);
            let free = &mut zone.free_frames.lock()[order.get()];
            let mut head = Pin::new_unchecked(free);
            head.add_head(node);
        }
        Ok(())
    }

    fn derive_head_frame(
        frame_number: FrameNumber,
        current_order: FrameOrder,
    ) -> Option<FrameNumber> {
        debug_assert!(frame_number.align_down(current_order) == frame_number);
        let frame_number = frame_number;

        let order = frame_number.get().trailing_zeros() as u8;
        let order = FrameOrder::new(order.min(MAX_ORDER.0));

        // 检查自己是否刚好是头
        if let FrameTag::Buddy = Frame::get_tag_relaxed(frame_number) {
            return Some(frame_number);
        }

        // 否则往前找
        for order in order.get()..=MAX_ORDER.get() {
            let order = FrameOrder::new(order as u8);
            let start = (frame_number - 1).align_down(order);
            if matches!(Frame::get_tag_relaxed(start), FrameTag::Buddy) {
                return Some(start);
            }
        }

        // 没找到说明这块内存没被管理
        None
    }

    /// 从 Buddy 块中剔除 range 覆盖的页
    fn exclude_from_buddy(
        &self,
        head: FrameNumber,
        current: FrameNumber,
        current_order: FrameOrder,
    ) -> Result<Option<FrameOrder>, FrameError> {
        let mut frame = UniqueFrames::from_allocator(Frame::get_raw(head), FrameOrder(0), self)
            .ok_or(FrameError::Conflict)?;

        // 检查是否已经被分配
        if !matches!(
            frame.get_tag(),
            FrameTag::Buddy | FrameTag::HardwareReserved
        ) {
            return Err(FrameError::IncorrectFrameType);
        }

        let buddy: &mut Buddy = (*frame).deref_mut().try_into()?;
        let order = buddy.order;
        let zone_type = buddy.zone_type;

        // 2 种情况：
        // 1. 被覆盖:   head.end > current.end
        // 2. 未被覆盖: head.end <= current.start

        // 情况 1：覆盖
        if order > current_order {
            debug_assert!(head + order.to_count().get() > current);

            let zone = &self.get_zone(zone_type).free_frames;
            let end = head + order.to_count().get();

            // 从链表中移除
            buddy.get_list().del(&mut zone.lock()[order.get()]);

            // 先处理左半部分
            let mut start = head;
            let mut order_ = order - 1;
            while start < current {
                let frame = Frame::get_raw(start);
                let mut frame = UniqueFrames::from_allocator(frame, order_, self).unwrap();
                let buddy: &mut Buddy = (*frame).deref_mut().try_into()?;

                buddy.get_list().del(&mut zone.lock()[order_.get()]);

                start = start + order_.to_count().get();
                order_ = order - 1;
            }

            // 再处理右半部分
            let mut start = current + current_order.to_count().get();
            while start < end {
                let order = start.get().trailing_zeros() as u8;
                let order = FrameOrder::new(order);
                let frame = Frame::get_raw(start);
                let mut frame = UniqueFrames::from_allocator(frame, order, self).unwrap();

                let buddy: &mut Buddy = (*frame).deref_mut().try_into()?;
                buddy.get_list().del(&mut zone.lock()[order.get()]);

                start = start + order.to_count().get();
            }
        }

        // 情况2: 未覆盖，不用处理

        Ok(Some(order))
    }
}

impl FrameAllocator for BuddyAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<UniqueFrames> {
        let mut order = order;
        let target_order = order;
        let mut buddy = None;

        while buddy.is_none() && order <= MAX_ORDER {
            let mut guard = self.get_zone(zone_type).free_frames.lock();
            let list_head = &mut guard[order.get()];

            buddy = if list_head.is_empty() {
                None
            } else {
                list_head.iter(Buddy::list_offset()).next()
            };

            if let Some(mut buddy) = buddy {
                let buddy = unsafe { buddy.as_mut() };
                buddy.get_list().del(list_head);

                let frame = unsafe { NonNull::from(Frame::from_child(buddy)) };
                let mut frame = UniqueFrames::from_allocator(frame, order, self).unwrap();
                drop(guard);

                if order != target_order {
                    self.split(zone_type, order, target_order, &mut frame);
                }

                let anonymous = ManuallyDrop::new(Anonymous::new(order));
                unsafe { frame.replace(FrameTag::Anonymous, FrameData { anonymous }) };

                frame.set_tail_frames();

                ALLOCATED_PAGES.fetch_add(order.to_count().get(), Ordering::Relaxed);
                return Some(ManuallyDrop::into_inner(frame));
            }
            order.0 += 1;
        }
        None
    }

    fn deallocate(&self, frame: &mut Frame) -> Result<(), FrameError> {
        let addr = frame.start_addr();

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let (zone_start, zone_end) = (
            zone_range.0.to_frame_number(),
            zone_range.1.to_frame_number(),
        );

        if !matches!(frame.get_tag(), FrameTag::Anonymous) {
            return Err(FrameError::IncorrectFrameType);
        }
        let order = unsafe { frame.get_data().anonymous.get_order() };
        let count = order.to_count().get();

        let buddy = ManuallyDrop::new(Buddy::new(order, zone_type));
        unsafe { frame.replace(FrameTag::Buddy, FrameData { buddy }) };
        let mut frame = UniqueFrames::from_allocator(NonNull::from(frame), order, self).unwrap();

        ALLOCATED_PAGES.fetch_sub(count, Ordering::Relaxed);

        let frame_number = frame.to_frame_number().get();
        let max_order = FrameOrder::new(frame_number.trailing_zeros() as u8);
        let max_order = max_order.min(MAX_ORDER);

        let range = (zone_start, zone_end);

        while order < max_order {
            match self.merge_once(frame, order, range) {
                Ok(success) => frame = success,
                Err(failed) => {
                    frame = failed;
                    break;
                }
            }
        }

        self.add_to_free_list(&mut frame, order, zone)
    }

    fn assign(&self, start: FrameNumber, order: FrameOrder) -> Result<UniqueFrames, FrameError> {
        debug_assert!(
            start.get() % order.to_count().get() == 0,
            "Assigned frame range must be aligned to its size"
        );

        let frame = Frame::get_raw(start);
        let mut frame =
            UniqueFrames::from_allocator(frame, order, self).ok_or(FrameError::Conflict)?;

        if order < MAX_ORDER && start >= RESERVED_END {
            // 查找 Buddy 块头部
            let head = Self::derive_head_frame(start, order);

            if let Some(head) = head {
                // 处理这个 Buddy 块
                self.exclude_from_buddy(head, start, order)?;
            }
        }
        // 如果已经是最大 order 了或者找不到，则无需往前找
        if matches!(frame.get_tag(), FrameTag::Uninited | FrameTag::Buddy) {
            let anonymous = ManuallyDrop::new(Anonymous::new(order));
            unsafe { frame.replace(FrameTag::Anonymous, FrameData { anonymous }) };
        } else {
            return Err(FrameError::Conflict);
        }

        frame.set_tail_frames();

        Ok(ManuallyDrop::into_inner(frame))
    }
}
