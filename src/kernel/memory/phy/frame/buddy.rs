use core::{
    mem::{ManuallyDrop, offset_of, zeroed},
    num::NonZeroUsize,
    ops::{Add, DerefMut, Sub},
    pin::Pin,
};

use crate::{
    kernel::memory::phy::frame::{
        FRAME_INFO_COUNT, Frame, FrameAllocator, FrameData, FrameError, FrameNumber, FrameRange,
        FrameTag, PAGE_SIZE, frame_count, reference::FrameMut, zone::ZoneType,
    },
    lib::rust::{list::ListHead, spinlock::Spinlock},
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
    pub fn from_frame<'a>(frame: &'a mut Frame) -> &'a mut Self {
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
    pub static_frames: Spinlock<ListHead<Frame>>,
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

        // 初始化静态分配链表
        unsafe {
            self.static_frames.init_with(|head| {
                Pin::new_unchecked(head).init();
            })
        };

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
            let frame = Frame::get(frame_num).unwrap();
            match frame.get_tag() {
                FrameTag::Free => {
                    // 获取E820块的范围
                    let block_range = unsafe { *frame.get_data().range };
                    frame_num = block_range.end + 1;
                    drop(frame);

                    self.add_free_block(&mut zone_state, block_range.start, block_range.end);
                }
                FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                    // 跳过保留区
                    let block_range = unsafe { frame.get_data().range };
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

            // 起始地址也要对齐到当前Order
            let max_order = if start.get() == 0 {
                MAX_ORDER - 1
            } else {
                FrameOrder::new((start.get().ilog2() as u8).min(MAX_ORDER.get() as u8 - 1))
            };

            // 计算在当前Zone内能分割的最大范围
            // 受限于：E820块末尾、Zone边界、最大Order大小
            let chunk_end = end
                .min(start + max_order.to_count().get() - 1)
                .min(zone_boundary);

            let order = FrameOrder::from_frame_count(chunk_end.count_from(start));

            // 初始化该Buddy块的Frame结构
            unsafe {
                let mut frame = Frame::get_mut(start).unwrap();

                frame.replace(
                    FrameTag::Buddy,
                    FrameData {
                        buddy: ManuallyDrop::new(Buddy { order, zone_type }),
                    },
                );

                let node = Pin::new_unchecked(frame.list.get_mut());
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
        frame: &mut FrameMut,
    ) {
        let mut split_order = FrameOrder::new(order.0 - 1);
        let mut next_frame = frame.next_frame(1 << split_order.get()).unwrap();

        while split_order.0 > target_order.0 {
            match next_frame.get_tag() {
                FrameTag::Unused => {
                    let buddy = ManuallyDrop::new(Buddy {
                        order: split_order,
                        zone_type,
                    });

                    unsafe {
                        next_frame.replace(FrameTag::Buddy, FrameData { buddy });

                        let node = Pin::new_unchecked(next_frame.list.get_mut());

                        let free =
                            &mut self.get_zone(zone_type).free_frames.lock()[split_order.get()];
                        let mut head = Pin::new_unchecked(free);

                        head.add_head(node);
                    }
                }
                _ => {
                    panic!("Split frame error: not a buddy frame");
                }
            }

            split_order.0 -= 1;
            next_frame = frame.next_frame(1 << split_order.get()).unwrap();
        }

        let buddy = Buddy::from_frame(frame);
        buddy.order = target_order;
    }

    fn merge_exact(&self, left: &mut FrameMut, right: &mut Buddy) {
        let new_order = right.order.0 + 1;
        let buddy = Buddy::from_frame(left);
        buddy.order = FrameOrder(new_order);

        let frame = Frame::from_child(right);
        unsafe {
            *frame.tag.get_mut() = FrameTag::Unused;

            let node = Pin::new_unchecked(left.list.get_mut());
            let free = &mut self.get_zone(right.zone_type).free_frames.lock()[new_order as usize];
            let mut head = Pin::new_unchecked(free);

            head.add_head(node);
        }
    }

    fn merge_once(&self, buddy: &mut Buddy, range: (FrameNumber, FrameNumber)) -> bool {
        let order = buddy.order + 1;

        let frame_number = FrameNumber::from_addr(buddy as *const _ as usize);

        let left = frame_number.get() & !(order.to_count().get() - 1);
        let left = FrameNumber::new(left);

        if range.0 < left && (left + order.to_count().get()) < range.1 {
            let mut left = match Frame::get_mut(left) {
                Some(left) => left,
                None => {
                    return false;
                }
            };

            let mut right = match left.next_frame(buddy.order.to_count().get()) {
                Some(right) => right,
                None => {
                    return false;
                }
            };

            self.merge_exact(&mut left, Buddy::from_frame(&mut right));
            true
        } else {
            false
        }
    }

    /// 将 Frame 添加回空闲链表
    fn add_to_free_list(
        &self,
        frame: &mut Frame,
        order: FrameOrder,
        zone: &Zone,
    ) -> Result<(), FrameError> {
        unsafe {
            let node = Pin::new_unchecked(frame.list.get_mut());
            let free = &mut zone.free_frames.lock()[order.get()];
            let mut head = Pin::new_unchecked(free);
            head.add_head(node);
        }
        Ok(())
    }

    fn derive_head_frame(frame_number: FrameNumber) -> Option<FrameNumber> {
        let max_order =
            FrameOrder::new((frame_number.get().ilog2() as u8).min(MAX_ORDER.get() as u8 - 1));
        let mut frame_number = frame_number;
        let mut tag;

        for order in 0..=max_order.get() {
            let mask = !((1 << order) - 1);
            frame_number = FrameNumber::new(frame_number.get() & mask);
            tag = Frame::get_tag_relaxed(frame_number);

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
        let mut frame = Frame::get_mut(frame_number).ok_or(FrameError::Conflict)?;

        // 检查是否为 Head Frame
        debug_assert!(!matches!(frame.get_tag(), FrameTag::Unused));
        // 检查是否已经被分配
        if !matches!(frame.get_tag(), FrameTag::Buddy) {
            debug_assert!(false, "Exclude frames error: not a buddy frame");
            return Err(FrameError::IncorrectFrameType);
        }

        let buddy = Buddy::from_frame(&mut frame);
        let order = buddy.order;
        let buddy_count = order.to_count().get();

        let zone_type = buddy.zone_type;
        let index = zone_type.index();
        let zone_end = FrameNumber::from_addr(zone_type.range().1);

        // 5 种情况：
        // 1. 完全覆盖：Buddy块完全在range内 → 标记Unused并移除
        // 2. 左半覆盖：range覆盖左半部分 → 将当前Buddy块分割
        // 3. 右半覆盖：range覆盖右半部分 → 将当前Buddy块分割
        // 4. 中间穿过：range跨越Buddy块中点 → 将当前Buddy块前后两部分分割
        // 5. 无交集：range与Buddy块无重叠 → 不处理

        // 计算 Buddy 块的左右边界（对齐到块大小）
        let left = frame_number.get() & !(buddy_count - 1);
        let left = FrameNumber::new(left);
        let right = left + buddy_count;

        // 情况 5：无交集 - range 与 Buddy 块完全不重叠
        if range.1 <= left || range.0 >= right {
            return Ok(None);
        }

        // 从链表中移除
        unsafe {
            let _zone = self.get_zone(zone_type).free_frames.lock();

            let node = Pin::new_unchecked(frame.list.get_mut());
            node.del();
        }

        // 情况 1：完全覆盖 - Buddy 块完全在 range 内
        if range.0 <= left && right <= range.1 {
            // 标记为未使用
            frame.set_tag(FrameTag::Unused);
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
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<FrameMut> {
        let mut order = order;
        let target_order = order;
        let mut frame = None;

        while frame.is_none() && order < MAX_ORDER {
            let mut guard = self.get_zone(zone_type).free_frames.lock();
            let list_head = &mut guard[order.get()];

            frame = if list_head.is_empty() {
                None
            } else {
                list_head.iter(offset_of!(Frame, list)).next()
            };

            if let Some(frame) = frame {
                let mut frame = FrameMut::new(frame).unwrap();

                let list = frame.list.get_mut();
                unsafe { Pin::new_unchecked(list) }.del();

                drop(guard);

                if order != target_order {
                    self.split(zone_type, order, target_order, &mut frame);
                }

                frame.set_tag(FrameTag::Allocated);

                return Some(frame);
            }
            order.0 += 1;
        }
        None
    }

    fn free(&self, frame: &mut Frame) -> Result<usize, FrameError> {
        let addr = frame.start_addr();

        let zone_type = ZoneType::from_address(addr);
        let zone = self.get_zone(zone_type);
        let zone_range = zone_type.range();
        let (zone_start, zone_end) = (
            FrameNumber::from_addr(zone_range.0),
            FrameNumber::new(zone_range.1),
        );

        if matches!(frame.get_tag(), FrameTag::Allocated) {
            // 仅释放Buddy类型的页
            let buddy = Buddy::from_frame(frame);
            let order = buddy.order;
            let count = order.to_count().get();

            // 尝试合并相邻的伙伴块，如果无法合并则添加回空闲链表
            if order < MAX_ORDER - 1 && self.merge_once(buddy, (zone_start, zone_end)) {
                Ok(count)
            } else {
                self.add_to_free_list(frame, order, zone).map(|_| count)
            }
        } else {
            Err(FrameError::IncorrectFrameType)
        }
    }

    fn assign(&self, start: FrameNumber, count: usize) -> Result<(), FrameError> {
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

        let mut head = self.static_frames.lock();
        let mut head = unsafe { Pin::new_unchecked(head.deref_mut()) };

        let mut frame = Frame::get_mut(start).ok_or(FrameError::Conflict)?;
        let node = unsafe { Pin::new_unchecked(frame.list.get_mut()) };

        head.add_head(node);

        Ok(())
    }
}
