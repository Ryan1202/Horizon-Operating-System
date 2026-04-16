use core::{
    cell::SyncUnsafeCell,
    mem::{self, ManuallyDrop, offset_of, size_of, zeroed},
    num::NonZeroUsize,
    ops::{Add, DerefMut, Sub},
    pin::Pin,
    ptr::NonNull,
    sync::atomic::Ordering,
};

use crate::{
    arch::{ArchFlushTlb, ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        KLINEAR_BASE, PageCacheType, ROOT_PT_LINEAR, VMEMMAP_BASE, VMEMMAP_END,
        arch::ArchMemory,
        frame::{
            ALLOCATED_PAGES, FRAMES, Frame, FrameAllocator, FrameData, FrameError, FrameNumber,
            FrameTag, PREALLOCATED_END_PHY, PREALLOCATED_START_PHY, VMEMMAP_MAPPED_PAGES,
            anonymous::Anonymous,
            assigned::AssignedFixed,
            early::VMAP_PER_PAGE,
            frame_count,
            reference::UniqueFrames,
            zone::{ZONE_COUNT, ZoneType},
        },
        page::{
            FlushTlb, PageNumber, current_root_pt,
            dyn_pages::DynPages,
            iter::{PtIter, PtStep},
            linear_table_ptr,
            lock::{NormalPtLock, PtPage},
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
        debug_assert!(
            order <= MAX_ORDER.0,
            "Frame order must not exceed max order"
        );
        FrameOrder(order)
    }

    pub const fn new_huge(order: u8) -> Self {
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

    pub fn replace_frame(self, frame: &mut Frame) {
        let buddy = ManuallyDrop::new(self);
        unsafe {
            frame.replace(FrameTag::Buddy, FrameData { buddy });
        }
    }
}

impl<'a> TryFrom<&'a mut Frame> for &'a mut Buddy {
    type Error = FrameError;

    fn try_from(value: &'a mut Frame) -> Result<Self, Self::Error> {
        match value.get_tag() {
            FrameTag::Buddy => Ok(unsafe { &mut value.get_data_mut().buddy }),
            _ => Err(FrameError::IncorrectFrameType),
        }
    }
}

pub struct Zone {
    pub free_frames: [ListHead<Buddy>; MAX_ORDER.0 as usize + 1],
}

impl Zone {
    pub fn get_free_list(&mut self, order: FrameOrder) -> Pin<&mut ListHead<Buddy>> {
        unsafe { Pin::new_unchecked(&mut self.free_frames[order.get()]) }
    }
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
    pub zones: [Spinlock<Zone>; ZONE_COUNT],
}

impl BuddyAllocator {
    pub const fn empty() -> Self {
        unsafe { zeroed() }
    }

    fn get_zone(&self, zone_type: ZoneType) -> &Spinlock<Zone> {
        &self.zones[zone_type.index()]
    }

    /// 初始化Buddy内存分配器
    /// 1. 初始化所有Zone的空闲链表
    /// 2. 遍历Frame，根据Zone类型和Buddy order分割内存
    pub fn init(&self) {
        // 初始化所有Zone的空闲链表
        self.init_zone_lists();

        // 扫描Frame，初始化Buddy分配器
        self.populate_zones();
    }

    /// 初始化所有Zone的空闲链表结构
    fn init_zone_lists(&self) {
        for i in 0..ZONE_COUNT {
            unsafe {
                self.zones[i].init_with(|v| {
                    for free in v.free_frames.iter_mut() {
                        Pin::new_unchecked(free).init();
                    }
                })
            };
        }
    }

    /// 遍历 vmemmap，根据Zone和order将Free页加入对应的空闲链表
    fn populate_zones(&self) {
        let mut zone_state = ZoneState::new();
        let root_pt = ROOT_PT_LINEAR;

        // 将位于 PREALLOCATED_START 所在页元数据的范围信息放到 PREALLOCATED_END 所在页的 Frame 中
        // 因为初始化时还不知道 PREALLOCATED_END 在哪
        let prealloc_start = unsafe { *PREALLOCATED_START_PHY.get() }.to_frame_number();
        let prealloc_end = unsafe { *PREALLOCATED_END_PHY.get() }.to_frame_number();

        {
            let frame = Frame::get_raw(prealloc_start);
            let mut range = unsafe { frame.as_ref().get_data().range };

            let start = prealloc_end;
            range.start = start;

            let mut frame = Frame::get_raw(prealloc_end);

            unsafe { frame.as_mut().replace(FrameTag::Free, FrameData { range }) };
        }

        let base = VMEMMAP_BASE.to_page_number();
        let start = base + prealloc_end.get() / VMAP_PER_PAGE;

        // 筛选出叶子节点
        let leaf_iter = PtIter::<ArchPageTable, NormalPtLock>::new(
            PtPage::from(root_pt as *const ArchPageTable),
            start,
            VMEMMAP_END.to_page_number(),
            linear_table_ptr::<ArchPageTable>,
        )
        .expect("failed to iterate vmemmap mappings")
        .filter_map(|x| match x {
            PtStep::Leaf { page, .. } => Some(page),
            _ => None,
        });

        let mut frame_number = FrameNumber::new(0);

        for page in leaf_iter {
            // 计算帧号
            let num = page.get() - VMEMMAP_BASE.to_page_number().get();

            let range_start = FrameNumber::new(num);
            let range_end = FrameNumber::new((num + 1).next_multiple_of(VMAP_PER_PAGE));

            frame_number = frame_number.max(range_start);

            while frame_number < range_end {
                let frame = Frame::get_raw(frame_number);

                match Frame::get_tag_relaxed(frame_number) {
                    FrameTag::Free => {
                        let frame =
                            UniqueFrames::from_allocator(frame, FrameOrder(0), self).unwrap();

                        let block_range = unsafe { frame.get_data().range };
                        frame_number = block_range.end;

                        self.add_free_block(&mut zone_state, block_range.start, block_range.end);
                    }
                    FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                        let reserved_end = unsafe { frame.as_ref().get_data().range.end };
                        frame_number = reserved_end;
                    }
                    FrameTag::Uninited | FrameTag::PageTable => {
                        frame_number = frame_number + 1;
                    }
                    tag => panic!("Buddy init: invalid frame tag: {:?}", tag),
                }
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
            let frame = Frame::get_raw(start);
            let mut frame = UniqueFrames::from_allocator(frame, FrameOrder(0), self).unwrap();

            Buddy::new(order, zone_type).replace_frame(&mut frame);

            let _ = self.add_to_free_list(&mut frame, order, zone);

            start = chunk_end;
        }
    }
}

fn is_conflict(frame_number: FrameNumber) -> bool {
    let tag = Frame::get_tag_relaxed(frame_number);
    match tag {
        FrameTag::Tail => {
            let head = unsafe { Frame::get_raw(frame_number).as_ref().get_data().range.start };
            is_conflict(head)
        }
        FrameTag::HardwareReserved
        | FrameTag::SystemReserved
        | FrameTag::BadMemory
        | FrameTag::Uninited => false,
        _ => true,
    }
}

impl BuddyAllocator {
    fn metadata_page_range(start: FrameNumber, order: FrameOrder) -> (PageNumber, PageNumber) {
        let count = order.to_count().get();
        let start_addr = VirtAddr::new(Frame::get_raw(start).as_ptr().addr()).page_align_down();
        let end_frame = start + count - 1;
        let end_addr =
            VirtAddr::new(Frame::get_raw(end_frame).as_ptr().addr() + size_of::<Frame>())
                .page_align_up();

        (start_addr.to_page_number(), end_addr.to_page_number())
    }

    fn map_vmemmap_page(&self, page: PageNumber) -> Result<(), FrameError> {
        let frame = self
            .allocate(ZoneType::LinearMem, FrameOrder::new(0))
            .or_else(|| self.allocate(ZoneType::MEM32, FrameOrder::new(0)))
            .ok_or(FrameError::OutOfFrames)?;

        let paddr = PhysAddr::from_frame_number(frame.frame_number());
        let ptr = (KLINEAR_BASE + paddr.as_usize()).as_mut_ptr::<u8>();
        unsafe { ptr.write_bytes(0, ArchPageTable::PAGE_SIZE) };

        let mut pages = DynPages::fixed(page, NonZeroUsize::new(1).unwrap());
        pages
            .map::<ArchPageTable>(frame, PageCacheType::WriteBack)
            .map_err(|_| FrameError::Conflict)?;

        VMEMMAP_MAPPED_PAGES.fetch_add(1, Ordering::Relaxed);
        ArchFlushTlb::flush_page(page);
        Ok(())
    }

    fn ensure_metadata_mapped(
        &self,
        start: FrameNumber,
        order: FrameOrder,
    ) -> Result<(), FrameError> {
        let root_pt = current_root_pt();
        let (page_start, page_end) = Self::metadata_page_range(start, order);

        let leaf_iter = PtIter::<ArchPageTable, NormalPtLock>::new(
            PtPage::from(root_pt as *const ArchPageTable),
            page_start,
            page_end,
            linear_table_ptr::<ArchPageTable>,
        )
        .ok_or(FrameError::Conflict)?
        .filter_map(|x| match x {
            PtStep::Leaf { page, .. } => Some(page),
            _ => None,
        });

        let mut expected_page = page_start;
        for page in leaf_iter {
            while expected_page < page {
                self.map_vmemmap_page(expected_page)?;
                expected_page = expected_page + 1;
            }
            expected_page = page + 1;
        }

        while expected_page < page_end {
            self.map_vmemmap_page(expected_page)?;
            expected_page = expected_page + 1;
        }

        Ok(())
    }

    fn conflict_check(&self, start: FrameNumber, order: FrameOrder) -> bool {
        let root_pt = current_root_pt();
        let end = start + order.to_count().get();
        let (page_start, page_end) = Self::metadata_page_range(start, order);

        let Some(iter) = PtIter::<ArchPageTable, NormalPtLock>::new(
            PtPage::from(root_pt as *const ArchPageTable),
            page_start,
            page_end,
            linear_table_ptr::<ArchPageTable>,
        ) else {
            return true;
        };

        let leaf_iter = iter.filter_map(|x| match x {
            PtStep::Leaf { page, .. } => Some(page),
            _ => None,
        });

        let mut expected_page = page_start;
        for page in leaf_iter {
            if page != expected_page {
                return true;
            }
            expected_page = page + 1;

            let num = page.get() - VMEMMAP_BASE.to_page_number().get();
            let range_start = FrameNumber::new(num);
            let range_end = range_start + VMAP_PER_PAGE;
            let overlap_start = start.max(range_start);
            let overlap_end = end.min(range_end);

            let mut frame_number = overlap_start;
            while frame_number < overlap_end {
                if is_conflict(frame_number) {
                    return true;
                }

                frame_number = frame_number + 1;
            }
        }

        expected_page != page_end
    }

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

            Buddy::new(split_order, zone_type).replace_frame(&mut next_frame);

            let node = Buddy::get_list((*next_frame).deref_mut().try_into().unwrap());

            {
                let mut zone = self.get_zone(zone_type).lock();
                let mut head = zone.get_free_list(split_order);

                head.add_tail(node);
            }

            mem::forget(next_frame);

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

            let mut zone = self.get_zone(zone_type).lock();
            let mut head = zone.get_free_list(FrameOrder(new_order));

            head.add_head(node);
        }
    }

    fn merge_once(
        &self,
        frame: ManuallyDrop<UniqueFrames>,
        current_order: FrameOrder,
        range: (FrameNumber, FrameNumber),
    ) -> Result<ManuallyDrop<UniqueFrames>, ManuallyDrop<UniqueFrames>> {
        let frame_number = frame.frame_number();

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
        zone: &Spinlock<Zone>,
    ) -> Result<(), FrameError> {
        let node = Buddy::get_list(frame.try_into()?);
        let mut zone = zone.lock();
        let mut head = zone.get_free_list(order);

        head.add_head(node);

        Ok(())
    }
}

impl FrameAllocator for BuddyAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<UniqueFrames> {
        let mut order = order;
        let target_order = order;

        while order <= MAX_ORDER {
            let mut zone = self.get_zone(zone_type).lock();
            let list_head = zone.get_free_list(order);
            let iter = list_head.iter(Buddy::list_offset()).next();

            if let Some(mut buddy) = iter {
                let buddy = unsafe { buddy.as_mut() };

                zone.get_free_list(order).del(buddy.get_list());

                let frame = unsafe { NonNull::from(Frame::from_child(buddy)) };
                let frame = UniqueFrames::from_allocator(frame, order, self).unwrap();
                let mut frame = ManuallyDrop::into_inner(frame);
                drop(zone);

                if order != target_order {
                    self.split(zone_type, order, target_order, &mut frame);
                }

                Anonymous::new(order).replace_frame(&mut frame);

                frame.set_tail_frames();

                ALLOCATED_PAGES.fetch_add(order.to_count().get(), Ordering::Relaxed);
                return Some(frame);
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

        Buddy::new(order, zone_type).replace_frame(frame);

        let mut frame = UniqueFrames::from_allocator(NonNull::from(frame), order, self).unwrap();

        ALLOCATED_PAGES.fetch_sub(count, Ordering::Relaxed);

        let frame_number = frame.frame_number().get();
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

        self.ensure_metadata_mapped(start, order)?;

        if self.conflict_check(start, order) {
            return Err(FrameError::Conflict);
        }

        let mut frame = Frame::get_raw(start);
        let original_tag = Frame::get_tag_relaxed(start);
        let original_range = match original_tag {
            FrameTag::HardwareReserved
            | FrameTag::SystemReserved
            | FrameTag::BadMemory
            | FrameTag::Free => unsafe { frame.as_ref().get_data().range },
            _ => crate::kernel::memory::frame::FrameRange {
                start,
                end: start + order.to_count().get(),
            },
        };

        AssignedFixed::new(order, original_tag, original_range)
            .replace_frame(unsafe { frame.as_mut() });

        let mut frame =
            UniqueFrames::from_allocator(frame, order, self).ok_or(FrameError::Conflict)?;
        frame.set_tail_frames();

        Ok(ManuallyDrop::into_inner(frame))
    }
}
