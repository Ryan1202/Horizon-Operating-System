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
            FrameNumber, FrameRange, FrameTag, frame_count,
            reference::UniqueFrames,
            zone::{ZONE_COUNT, ZoneType},
        },
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::Spinlock,
    },
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
        debug_assert!(count <= MAX_ORDER.to_count().get());
        FrameOrder::new(count.next_power_of_two().ilog2() as u8)
    }

    pub const fn from_size(size: usize) -> Self {
        debug_assert!(size > 0);
        let frame_count = frame_count(size);
        Self::from_frame_count(frame_count)
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
            FrameTag::Buddy | FrameTag::Allocated => {
                Ok(unsafe { value.get_data_mut().buddy.deref_mut() })
            }
            _ => Err(FrameError::IncorrectFrameType),
        }
    }
}

pub struct Zone {
    pub free_frames: Spinlock<[ListHead<Buddy>; MAX_ORDER.0 as usize]>,
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
            current_zone_end: zone_end.to_frame_number(),
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
            zone_end = zone_type.range().1.to_frame_number();
        }
        self.current_zone_end = zone_end;
        (zone_type, zone_end)
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

                    let block_range = unsafe { *frame.get_data().range };
                    frame_number = block_range.end + 1;

                    self.add_free_block(&mut zone_state, block_range.start, block_range.end);
                }
                FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                    // 跳过保留区
                    let range = unsafe { frame.as_ref().get_data().range };

                    frame_number = range.end + 1;
                }
                FrameTag::Unavailable => {
                    // 不可用页，可能是跨越了两个不同区域，继续下一个
                    frame_number = frame_number + 1;
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

            // 起始地址也要对齐到当前Order
            let max_order = if start.get() == 0 {
                MAX_ORDER - 1
            } else {
                FrameOrder::new((start.get().trailing_zeros() as u8).min(MAX_ORDER.get() as u8 - 1))
            };

            // 计算在当前Zone内能分割的最大范围
            // 受限于：E820块末尾、Zone边界、最大Order大小
            let chunk_end = end
                .min(start + max_order.to_count().get() - 1)
                .min(zone_boundary);

            let order = FrameOrder::from_frame_count(chunk_end.count_from(start));

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

                let node = Buddy::get_list((*frame).deref_mut().try_into().unwrap());
                let free = &mut zone.free_frames.lock()[order.get()];
                let mut head = Pin::new_unchecked(free);
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
        frame: &mut UniqueFrames,
    ) {
        let mut split_order = FrameOrder::new(order.0 - 1);
        let mut next_frame = frame.split();

        while split_order.0 > target_order.0 {
            match next_frame.get_tag() {
                FrameTag::Buddy => {
                    panic!("Split error: frame is already a buddy block");
                }
                FrameTag::Tail | FrameTag::Unavailable => {}
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

    fn merge_exact(&self, mut left: ManuallyDrop<UniqueFrames>, right: ManuallyDrop<UniqueFrames>) {
        let buddy: &mut Buddy = (*left).deref_mut().try_into().unwrap();

        let new_order = buddy.order.0 + 1;
        buddy.order = FrameOrder(new_order);

        let zone_type = buddy.zone_type;

        let mut frame = right;
        unsafe {
            frame.set_tag(FrameTag::Unavailable);

            let node = Buddy::get_list((*left).deref_mut().try_into().unwrap());
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
    ) -> Result<(), ManuallyDrop<UniqueFrames>> {
        let frame_number = frame.to_frame_number();

        let new_order = current_order + 1;
        let count = new_order.to_count().get();

        let low = frame_number.align_down(new_order);
        let high = low + count;

        if range.0 >= low || high >= range.1 {
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

    fn derive_head_frame(frame_number: FrameNumber) -> Option<FrameNumber> {
        let max_order =
            FrameOrder::new((frame_number.get().ilog2() as u8).min(MAX_ORDER.get() as u8 - 1));
        let frame_number = frame_number;
        let mut tag;

        for order in 0..=max_order.get() {
            let order = FrameOrder::new(order as u8);
            tag = Frame::get_tag_relaxed(frame_number.align_down(order));

            if let FrameTag::Buddy = tag {
                return Some(frame_number);
            }
        }
        None
    }

    /// 从 Buddy 块中剔除 range 覆盖的页
    fn exclude_from_buddy(
        &self,
        frame_number: FrameNumber,
        range: (FrameNumber, FrameNumber),
    ) -> Result<Option<FrameOrder>, FrameError> {
        let mut frame =
            UniqueFrames::from_allocator(Frame::get_raw(frame_number), FrameOrder(0), self)
                .ok_or(FrameError::Conflict)?;

        // 检查是否为 Head Frame
        debug_assert!(!matches!(
            frame.get_tag(),
            FrameTag::Unavailable | FrameTag::Tail
        ));
        // 检查是否已经被分配
        if !matches!(frame.get_tag(), FrameTag::Buddy) {
            debug_assert!(false, "Exclude frames error: not a buddy frame");
            return Err(FrameError::IncorrectFrameType);
        }

        let buddy: &mut Buddy = (*frame).deref_mut().try_into()?;
        let order = buddy.order;
        let buddy_count = order.to_count().get();

        let zone_type = buddy.zone_type;
        let index = zone_type.index();
        let zone_end = zone_type.range().1.to_frame_number();

        // 5 种情况：
        // 1. 完全覆盖：Buddy块完全在range内 → 标记Unused并移除
        // 2. 左半覆盖：range覆盖左半部分 → 将当前Buddy块分割
        // 3. 右半覆盖：range覆盖右半部分 → 将当前Buddy块分割
        // 4. 中间穿过：range跨越Buddy块中点 → 将当前Buddy块前后两部分分割
        // 5. 无交集：range与Buddy块无重叠 → 不处理

        // 计算 Buddy 块的左右边界（对齐到块大小）
        let left = frame_number.align_down(order);
        let right = left + buddy_count;

        // 情况 5：无交集 - range 与 Buddy 块完全不重叠
        if range.1 <= left || range.0 >= right {
            return Ok(None);
        }

        // 从链表中移除
        {
            let mut zone = self.get_zone(zone_type).free_frames.lock();

            Buddy::get_list((*frame).deref_mut().try_into()?).del(&mut zone[order.get()]);
        }

        // 情况 1：完全覆盖 - Buddy 块完全在 range 内
        if range.0 <= left && right <= range.1 {
            // 标记为不可使用
            frame.set_tag(FrameTag::Unavailable);
            return Ok(Some(order));
        }

        // 情况 2, 3, 4：部分覆盖 - 将不在 range 内的部分重新加入 Buddy 系统
        let mut zone_state = ZoneState {
            current_index: index,
            current_zone_end: zone_end,
        };

        // 左侧未覆盖部分 [left, range.0)
        if left < range.0 {
            let _range = ManuallyDrop::new(FrameRange {
                start: left,
                end: range.0,
            });
            unsafe { frame.replace(FrameTag::Free, FrameData { range: _range }) };

            self.add_free_block(&mut zone_state, left, range.0 - 1);
        }

        // 右侧未覆盖部分 [range.1, right)
        if range.1 < right {
            let _range = ManuallyDrop::new(FrameRange {
                start: range.1,
                end: right,
            });
            unsafe { frame.replace(FrameTag::Free, FrameData { range: _range }) };

            self.add_free_block(&mut zone_state, range.1, right - 1);
        }

        // range 内的部分不加入 Buddy 系统（已从链表移除，保持 Unused 或由调用者处理）
        assert!(zone_state.current_index == index);

        Ok(Some(order))
    }
}

impl FrameAllocator for BuddyAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<UniqueFrames> {
        let mut order = order;
        let target_order = order;
        let mut buddy = None;

        while buddy.is_none() && order < MAX_ORDER {
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

                frame.set_tag(FrameTag::Allocated);

                frame.set_tail_frames();

                ALLOCATED_PAGES.fetch_add(order.to_count().get(), Ordering::Relaxed);
                return Some(ManuallyDrop::into_inner(frame));
            }
            order.0 += 1;
        }
        None
    }

    fn deallocate(&self, frame: &mut Frame) -> Result<usize, FrameError> {
        let addr = frame.start_addr();

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let (zone_start, zone_end) = (
            zone_range.0.to_frame_number(),
            zone_range.1.to_frame_number(),
        );

        let buddy: &mut Buddy = frame.try_into()?;
        let order = buddy.order;
        let count = order.to_count().get();

        let mut frame = UniqueFrames::from_allocator(NonNull::from(frame), order, self).unwrap();
        frame.set_tag(FrameTag::Buddy);

        ALLOCATED_PAGES.fetch_sub(count, Ordering::Relaxed);

        // 顺序传递：
        // Result<UniqueFrames, UniqueFrames>
        // -> Result<(), UniqueFrames>
        // -> Result<(), FrameError>
        // -> Result<usize, FrameError>
        (if order < MAX_ORDER - 1 {
            Ok(frame)
        } else {
            Err(frame)
        })
        .and_then(|frame| self.merge_once(frame, order, (zone_start, zone_end)))
        .or_else(|mut frame| self.add_to_free_list(&mut frame, order, zone))
        .map(|_| count)
    }

    fn assign(&self, start: FrameNumber, count: usize) -> Result<UniqueFrames, FrameError> {
        let max_order_count = (MAX_ORDER - 1).to_count().get();
        let mut current = start;
        let end = start + count;
        let range = (start, end);

        while current < end {
            // 查找包含 current 的 Buddy 块头部
            let mut head_frame = Self::derive_head_frame(current);

            // 如果找不到 Buddy 块，跳到下一个可能的对齐位置
            while head_frame.is_none() && current < end {
                current = FrameNumber::new((current.get() + 1).next_multiple_of(max_order_count));
                if current >= end {
                    break;
                }
                head_frame = Self::derive_head_frame(current);
            }

            if let Some(head) = head_frame {
                // 处理这个 Buddy 块
                let result = self.exclude_from_buddy(head, range)?;

                if let Some(order) = result {
                    // 推进到该 Buddy 块的末尾
                    let buddy_count = order.to_count().get();
                    current = head + buddy_count;
                } else {
                    // 无交集，推进到下一个可能的位置
                    current = current + 1;
                }
            } else {
                // 没找到更多 Buddy 块，结束
                break;
            }
        }

        let mut frame = UniqueFrames::from_allocator(Frame::get_raw(start), FrameOrder(0), self)
            .ok_or(FrameError::Conflict)?;

        frame.set_tail_frames();

        Ok(ManuallyDrop::into_inner(frame))
    }
}
