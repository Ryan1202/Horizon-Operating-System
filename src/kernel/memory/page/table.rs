use core::marker::PhantomData;

use crate::{
    arch::{PhysAddr, VirtAddr},
    kernel::memory::{
        KERNEL_BASE, KLINEAR_BASE, MemoryError, PageCacheType,
        arch::ArchMemory,
        frame::{
            Frame, FrameNumber, buddy::FrameOrder, early::early_allocate_pages,
            options::FrameAllocOptions, reference::UniqueFrames, zone::ZoneType,
        },
        page::{
            PageFlags, PageNumber,
            dyn_pages::DynPages,
            iter::{PtIter, PtStep},
            lock::{EarlyPtLock, NormalPtLock, PageLock, PtPage, PtRwLock},
            options::PageAllocOptions,
        },
    },
};

/// 页表条目操作 trait
///
/// 每个架构为其 PTE 类型实现此 trait。
pub trait PageTableEntry: Sized {
    /// 创建一个无效的条目
    fn new_absent() -> Self;

    /// 创建一个映射到物理页帧的条目。
    fn new_mapped(frame: FrameNumber, flags: PageFlags, page_level: usize) -> Self;

    /// 是否有效
    fn is_present(&self) -> bool;

    /// 如果有效，返回映射的物理页号；否则返回 None
    fn frame_number(&self) -> Option<FrameNumber>;

    /// 获取当前 flags
    fn flags(&self, page_level: u8) -> PageFlags;

    /// 修改已有条目的 flags，不改变物理页帧。
    fn set_flags(&mut self, flags: PageFlags, page_level: usize);

    /// 是否是大页条目（用于多级页表中跳过下一级）
    fn is_huge(&self, page_level: u8) -> bool {
        self.flags(page_level).huge_page
    }

    /// 清空条目（设为 absent）
    fn clear(&mut self) {
        *self = Self::new_absent();
    }
}

pub trait PageTableEntrySlot {
    type Entry: PageTableEntry;

    fn read(&self) -> Self::Entry;
    fn write(&self, entry: Self::Entry);
}

#[derive(Clone, Copy)]
pub struct MappingChunk {
    start_vir: PageNumber,
    start_phy: FrameNumber,
    count: usize,
}

impl MappingChunk {
    pub const fn new(start_vir: PageNumber, start_phy: FrameNumber, count: usize) -> Self {
        Self {
            start_vir,
            start_phy,
            count,
        }
    }

    pub const fn vir_start(&self) -> PageNumber {
        self.start_vir
    }

    pub const fn phy_start(&self) -> FrameNumber {
        self.start_phy
    }

    pub const fn vir_end(&self) -> PageNumber {
        self.start_vir + self.count
    }

    pub const fn phy_end(&self) -> FrameNumber {
        self.start_phy + self.count
    }

    pub const fn page_count(&self) -> usize {
        self.count
    }

    pub fn try_expand(
        &mut self,
        start_vir: PageNumber,
        start_phy: FrameNumber,
        count: usize,
    ) -> Option<()> {
        if self.vir_end() == start_vir && self.phy_end() == start_phy {
            self.count += count;
            Some(())
        } else {
            None
        }
    }
}

/// 多级页表结构定义。
///
/// 架构实现只提供页表几何常量、entry 类型和 entry slot 访问。
pub trait PageTable: ArchMemory + Sized {
    type Entry: PageTableEntry;
    type EntrySlot: PageTableEntrySlot<Entry = Self::Entry>;

    const PAGE_OFFSET_BITS: usize = Self::PAGE_BITS;
    const LEVELS: usize;
    const INDEX_BITS: &'static [usize];
    const LEVEL_SHIFTS: &'static [usize];
    const LEVEL_COUNTS: &'static [usize];
    const LEVEL_MASKS: &'static [usize];
    const HUGE_PAGE: &'static [bool];

    fn get_entry(&self, index: usize) -> Self::EntrySlot;

    fn top_level() -> usize {
        Self::LEVELS - 1
    }

    fn entry_index(page_number: PageNumber, level: usize) -> usize {
        debug_assert!(level < Self::LEVELS);

        (page_number.get() >> Self::LEVEL_SHIFTS[level]) & ((1 << Self::INDEX_BITS[level]) - 1)
    }
}

pub struct PageTableOps<T: PageTable>(PhantomData<T>);

impl<T: PageTable> PageTableOps<T> {
    /// 在内核早期阶段映射虚拟页到物理页帧
    ///
    /// # Safety
    ///
    /// 只能在单核环境下使用，且调用者必须保证映射范围不重叠且线性地址空间足够。
    pub unsafe fn early_map(
        root_pt: *const usize,
        page: PageNumber,
        frame: FrameNumber,
        count: usize,
        linear_end: FrameNumber,
        flags: PageFlags,
        init_lock: bool,
    ) -> Result<(), PageTableError>
    where
        for<'a> EarlyPtLock: PtRwLock<'a, T>,
    {
        let end = page + count;

        let mut allocate_table = || {
            let frame = early_allocate_pages(1).to_frame_number();
            let ptr = if frame < linear_end {
                linear_table_ptr::<T>(frame)
            } else {
                kernel_table_ptr::<T>(frame)
            };
            unsafe { (ptr as *mut usize).write_bytes(0, T::PAGE_SIZE) };

            if init_lock {
                Frame::init_page_table_frame(frame);
            }
            Ok((frame, ptr))
        };

        map_with_iter::<T, EarlyPtLock>(
            PtPage::from(root_pt as *const T),
            page,
            end,
            frame,
            flags,
            kernel_table_ptr::<T>,
            &mut allocate_table,
        )
    }

    /// 将虚拟页映射到物理页帧
    ///
    /// 如果中间级页表不存在，使用 `frame_alloc` 分配新的页表帧。
    /// 如果目标 PTE 已经 present，返回 `EntryAlreadyMapped`。
    pub fn map(
        root_pt: *const usize,
        pages: &mut DynPages,
        offset: usize,
        frames: &mut UniqueFrames,
        flags: PageFlags,
    ) -> Result<(), MemoryError>
    where
        for<'a> NormalPtLock: PtRwLock<'a, T>,
    {
        let page_count = frames.get_order().to_count().get();

        let page_start = pages.start_addr().to_page_number() + offset;
        let page_end = page_start + page_count;

        let map_flags = flags.huge_page(frames.get_order().get() >= 9);
        let mut allocate_table = || alloc_table::<T>();

        map_with_iter::<T, NormalPtLock>(
            PtPage::from(root_pt as *const T),
            page_start,
            page_end,
            frames.frame_number(),
            map_flags,
            linear_table_ptr::<T>,
            &mut allocate_table,
        )
        .map_err(MemoryError::from)
    }

    /// 取消虚拟页的映射
    ///
    /// 返回之前映射的物理页帧。调用者负责调用 TLB flush。
    pub fn unmap(
        root_pt: *const usize,
        pages: &mut DynPages,
        offset: usize,
        order: FrameOrder,
    ) -> Result<(), PageTableError>
    where
        for<'a> NormalPtLock: PtRwLock<'a, T>,
    {
        let page_number = (pages.start_addr() + offset * T::PAGE_SIZE).to_page_number();
        let remaining = order.to_count().get();
        let root = PtPage::from(root_pt as *const T);

        let mut iter = PtIter::<T, NormalPtLock>::new(
            root,
            page_number,
            page_number + remaining,
            linear_table_ptr::<T>,
        )
        .ok_or(PageTableError::EntryAbsent)?;

        while let Some(step) = iter.next() {
            if let PtStep::Leaf { page, level, .. } = step {
                let guard = iter.get_mut().ok_or(PageTableError::EntryAbsent)?;

                let index = T::entry_index(page, level);
                guard.get_entry(index).write(T::Entry::new_absent());
            }
        }

        Ok(())
    }

    /// 翻译虚拟地址到物理地址（不修改页表）
    pub fn translate(root_pt: *const usize, vaddr: VirtAddr) -> Result<PhysAddr, PageTableError>
    where
        for<'a> NormalPtLock: PtRwLock<'a, T>,
    {
        let page = vaddr.to_page_number();
        let root = PtPage::from(root_pt as *const T);
        let mut iter = PtIter::<T, NormalPtLock>::new(root, page, page + 1, linear_table_ptr::<T>)
            .ok_or(PageTableError::EntryAbsent)?;

        let step = iter.next().ok_or(PageTableError::EntryAbsent)?;

        if let PtStep::Leaf { frame, level, .. } = step {
            let offset =
                vaddr.as_usize() & ((1 << (T::LEVEL_SHIFTS[level] + T::PAGE_OFFSET_BITS)) - 1);
            let phys = PhysAddr::from_frame_number(frame).as_usize() + offset;
            Ok(PhysAddr::new(phys))
        } else {
            Err(PageTableError::EntryAbsent)
        }
    }

    /// 修改已有映射的 flags（不改变物理地址）
    pub fn update_flags(
        root_pt: *const usize,
        pages: &mut DynPages,
        flags: PageFlags,
    ) -> Result<(), PageTableError>
    where
        for<'a> NormalPtLock: PtRwLock<'a, T>,
    {
        let root = PtPage::from(root_pt as *const T);
        let page_number = pages.start_addr().to_page_number();

        let mut page_lock =
            PageLock::<T, NormalPtLock>::lock_page(root, page_number, linear_table_ptr::<T>)
                .ok_or(PageTableError::EntryAbsent)?;

        let level = page_lock.level().ok_or(PageTableError::EntryAbsent)?;
        let index = T::entry_index(page_number, level);
        let entry_ptr = page_lock
            .get_mut()
            .ok_or(PageTableError::EntryAbsent)?
            .get_entry(index);

        let mut entry = entry_ptr.read();
        entry.set_flags(flags, level);
        entry_ptr.write(entry);

        Ok(())
    }
}

pub const fn kernel_table_ptr<T: PageTable>(frame: FrameNumber) -> *const T {
    let kernel_start_phy = 0;
    let phys = PhysAddr::from_frame_number(frame).as_usize();

    (KERNEL_BASE + (phys - kernel_start_phy)).as_ptr()
}

pub const fn linear_table_ptr<T: PageTable>(frame: FrameNumber) -> *const T {
    (KLINEAR_BASE + PhysAddr::from_frame_number(frame).as_usize()).as_ptr()
}

fn alloc_table<T: PageTable>() -> Result<(FrameNumber, *const T), PageTableError> {
    let frame_options = FrameAllocOptions::new().fallback(&[ZoneType::LinearMem]);
    let page_options = PageAllocOptions::new(frame_options)
        .contiguous(true)
        .zeroed(true);

    let mut page = page_options
        .allocate()
        .map_err(|_| PageTableError::FrameAllocationFailed)?;
    let frame = page
        .get_frame()
        .ok_or(PageTableError::FrameAllocationFailed)?;

    let frame = frame.frame_number();

    Frame::init_page_table_frame(frame);

    Ok((frame, linear_table_ptr::<T>(frame)))
}

fn map_with_iter<'a, T, L>(
    root: PtPage<T>,
    page_start: PageNumber,
    page_end: PageNumber,
    frame: FrameNumber,
    flags: PageFlags,
    phy2vir: fn(FrameNumber) -> *const T,
    allocate_table: &mut impl FnMut() -> Result<(FrameNumber, *const T), PageTableError>,
) -> Result<(), PageTableError>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T> + 'a,
{
    let mut lock = PageLock::<T, L>::lock_page(root, page_start, phy2vir)
        .ok_or(PageTableError::EntryAbsent)?;

    let mut current = page_start;
    let mut current_frame = frame;
    while current < page_end {
        let base = lock.base();
        let level = lock.level().ok_or(PageTableError::InvalidLevel)?;

        let index = T::entry_index(current, level);

        let start = current.max(base);
        let end = page_end.min(base + T::LEVEL_COUNTS[level + 1]);

        assert!(start < end);

        let count = end.get() - start.get();
        let use_huge = (current_frame.get() & !T::LEVEL_MASKS[level]) == 0
            && count == T::LEVEL_COUNTS[level]
            && T::HUGE_PAGE[level];

        let guard = lock.get().unwrap();
        let entry = guard.get_entry(index).read();
        if entry.is_present() {
            if use_huge || level == 0 || entry.is_huge(level as u8) {
                return Err(PageTableError::EntryAlreadyMapped);
            } else {
                lock.push(index).unwrap();
                continue;
            }
        }

        let guard = lock.get_mut().unwrap();
        let entry_ptr = guard.get_entry(index);
        if use_huge {
            entry_ptr.write(T::Entry::new_mapped(current_frame, flags, level));

            current = current + T::LEVEL_COUNTS[level];
            current_frame = current_frame + T::LEVEL_COUNTS[level];
        } else if level == 0 {
            entry_ptr.write(T::Entry::new_mapped(
                current_frame,
                flags.huge_page(false),
                level,
            ));

            current = current + T::LEVEL_COUNTS[level];
            current_frame = current_frame + T::LEVEL_COUNTS[level];
        } else {
            let (table_frame, _) = allocate_table()?;
            entry_ptr.write(T::Entry::new_mapped(
                table_frame,
                flags.huge_page(false).cache_type(PageCacheType::WriteBack),
                level,
            ));
            lock.push(index).unwrap();
            continue;
        }

        if index + 1 >= (1 << T::INDEX_BITS[level]) {
            lock.pop().ok_or(PageTableError::InvalidLevel)?;
            continue;
        }
    }

    Ok(())
}

/// 页表操作错误
#[derive(Debug, Clone)]
pub enum PageTableError {
    /// PTE 不存在
    EntryAbsent,
    /// PTE 已经映射到其他物理页
    EntryAlreadyMapped,
    /// 为页表结构分配 Frame 失败
    FrameAllocationFailed,
    /// 请求的页表层级无效
    InvalidLevel,
}
