use core::{
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ops::{Add, Sub},
};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        KLINEAR_SIZE, MemoryError,
        arch::ArchMemory,
        frame::{
            Frame, FrameNumber, FrameTag,
            buddy::FrameOrder,
            reference::{SharedFrames, UniqueFrames},
        },
        page::{dyn_pages::DynPages, options::PageAllocOptions},
        vir_base_addr,
        vmalloc::vfree,
    },
};

pub mod dyn_pages;
mod flags;
pub mod options;
pub(super) mod range;
mod tlb;
pub(super) mod vmap;

pub use flags::PageFlags;
pub use tlb::FlushTlb;

/// 页表条目操作 trait
///
/// 每个架构为其 PTE 类型实现此 trait。
pub trait PageEntry: Sized {
    /// 创建一个无效的条目
    fn new_absent() -> Self;

    /// 创建一个有效条目，映射到 frame 并设置 flags
    fn new_mapped(frame: FrameNumber, flags: PageFlags) -> Self;

    /// 是否有效
    fn is_present(&self) -> bool;

    /// 如果有效，返回映射的物理页号；否则返回 None
    fn frame_number(&self) -> Option<FrameNumber>;

    /// 获取当前 flags
    fn flags(&self) -> PageFlags;

    /// 修改 flags（保留物理地址不变）
    fn set_flags(&mut self, flags: PageFlags);

    /// 是否是大页条目（用于多级页表中跳过下一级）
    fn is_huge(&self) -> bool {
        self.flags().huge_page
    }

    /// 清空条目（设为 absent）
    fn clear(&mut self) {
        *self = Self::new_absent();
    }
}

/// 多级页表遍历器。
///
/// 每个架构提供一个实现，负责：
/// - 将虚拟地址拆分为各级页表索引
/// - 遍历页表层级并找到目标 PTE
/// - 按需为中间级页表分配 Frame
///
/// 泛型参数 `A: ArchMemory` 提供架构常量。
pub trait PageTableWalker {
    type Entry: PageEntry;

    /// 将虚拟页映射到物理页帧
    ///
    /// 如果中间级页表不存在，使用 `frame_alloc` 分配新的页表帧。
    /// 如果目标 PTE 已经 present，返回 `EntryAlreadyMapped`。
    fn map(
        pages: &mut DynPages,
        offset: usize,
        frame: FrameNumber,
        flags: PageFlags,
    ) -> Result<(), MemoryError>;

    /// 取消虚拟页的映射
    ///
    /// 返回之前映射的物理页帧。调用者负责调用 TLB flush。
    fn unmap(pages: &mut DynPages, offset: usize) -> Result<(), PageTableError>;

    /// 翻译虚拟地址到物理地址（不修改页表）
    fn translate(vaddr: VirtAddr) -> Result<PhysAddr, PageTableError>;

    /// 修改已有映射的 flags（不改变物理地址）
    fn update_flags(pages: &mut DynPages, flags: PageFlags) -> Result<(), PageTableError>;

    /// 批量映射
    ///
    /// 默认实现逐页调用 map()。架构可覆盖以使用大页优化。
    fn map_range(
        pages: &mut DynPages,
        frame_start: FrameNumber,
        offset: usize,
        count: usize,
        flags: PageFlags,
    ) -> Result<(), MemoryError> {
        let frame_number = frame_start;
        for i in 0..count {
            Self::map(pages, offset + i, frame_number + i, flags)?;
        }
        Ok(())
    }

    /// 批量取消映射
    fn unmap_range(
        pages: &mut DynPages,
        offset: usize,
        count: usize,
    ) -> Result<(), PageTableError> {
        for i in 0..count {
            Self::unmap(pages, offset + i)?;
        }
        Ok(())
    }
}

/// 页表操作错误
#[derive(Debug, Clone)]
pub enum PageTableError {
    /// PTE 不存在
    EntryNotPresent,
    /// PTE 已经映射到其他物理页
    EntryAlreadyMapped,
    /// 为页表结构分配 Frame 失败
    FrameAllocationFailed,
    /// 请求的页表层级无效
    InvalidLevel,
}

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct PageNumber(NonZeroUsize);

impl PageNumber {
    pub const fn new(num: NonZeroUsize) -> Self {
        PageNumber(num)
    }

    pub const fn get(&self) -> NonZeroUsize {
        self.0
    }

    pub const fn to_addr(&self) -> VirtAddr {
        VirtAddr::new(self.0.get() * ArchPageTable::PAGE_SIZE)
    }
}

impl Add<usize> for PageNumber {
    type Output = Self;

    fn add(self, rhs: usize) -> Self::Output {
        PageNumber(
            self.0
                .checked_add(rhs)
                .expect("PageNumber addition overflow"),
        )
    }
}

impl Sub<usize> for PageNumber {
    type Output = Self;

    fn sub(self, rhs: usize) -> Self::Output {
        PageNumber(
            NonZeroUsize::new(self.0.get() - rhs).expect("PageNumber substraction underflow"),
        )
    }
}

pub enum Pages<'a> {
    Fixed((UniqueFrames, usize)),
    Dynamic(&'a mut DynPages),
}

impl<'a> Pages<'a> {
    pub fn start_addr(&self) -> VirtAddr {
        match self {
            Pages::Fixed((frame, _)) => vir_base_addr() + frame.start_addr().as_usize(),
            Pages::Dynamic(vpages) => vpages.start_addr(),
        }
    }

    pub fn get_ptr<T>(&mut self) -> *mut T {
        self.start_addr().as_mut_ptr()
    }

    pub fn get_frame(&mut self) -> Option<&mut UniqueFrames> {
        match self {
            Pages::Fixed((frame, _)) => Some(frame),
            Pages::Dynamic(vpages) => vpages.first_frame.as_mut(),
        }
    }

    pub fn get_count(&self) -> usize {
        match self {
            Pages::Fixed((_, count)) => *count,
            Pages::Dynamic(vpages) => vpages.frame_count,
        }
    }
}

pub fn kmalloc_pages<'a>(count: NonZeroUsize) -> Result<Pages<'a>, MemoryError> {
    let order = FrameOrder::new(count.get().next_power_of_two().ilog2() as u8);

    let page_options = PageAllocOptions::kernel(order);

    let pages = page_options.allocate()?;

    Ok(pages)
}

#[unsafe(export_name = "kmalloc_pages")]
pub fn kmalloc_pages_c(count: usize) -> *mut core::ffi::c_void {
    let result = NonZeroUsize::new(count)
        .ok_or(MemoryError::InvalidSize(count))
        .and_then(kmalloc_pages);

    match result {
        Ok(pages) => {
            let mut pages = ManuallyDrop::new(pages);
            pages.get_ptr()
        }
        Err(e) => {
            printk!(
                "WARNING: calling kmalloc_pages_c failed: count = {:#x}, error = {:?}\n",
                count,
                e
            );
            core::ptr::null_mut()
        }
    }
}

pub fn kfree_pages(vaddr: VirtAddr) -> Result<(), MemoryError> {
    let linear_start = vir_base_addr();
    let linear_end = linear_start + KLINEAR_SIZE;

    // 如果在内核线性映射区，则无需释放虚拟页
    if vaddr >= linear_start && vaddr < linear_end {
        let phy_addr = PhysAddr::new(vaddr.offset_from(linear_start));
        let frame_number = phy_addr.to_frame_number();
        let frame = Frame::get_raw(frame_number);

        match Frame::get_tag_relaxed(frame_number) {
            FrameTag::Unavailable
            | FrameTag::HardwareReserved
            | FrameTag::SystemReserved
            | FrameTag::Tail => Err(MemoryError::UnavailableFrame),
            FrameTag::Free => panic!(
                "Attempt to free frame with unavailable tag at vaddr: {:#x}",
                vaddr.as_usize()
            ),
            _ => unsafe {
                if UniqueFrames::try_from_raw(frame).is_none() {
                    let _ = SharedFrames::from_raw(frame, FrameOrder::new(0))
                        .ok_or(MemoryError::UnavailableFrame)?;
                }

                Ok(())
            },
        }
    } else {
        vfree(vaddr)
    }
}

#[unsafe(export_name = "kfree_pages")]
pub fn kfree_pages_c(ptr: usize) -> i32 {
    match kfree_pages(VirtAddr::new(ptr)) {
        Ok(_) => 0,
        Err(e) => {
            printk!(
                "WARNING: calling kfree_pages from C failed: vaddr = {:#x}, error = {:?}\n",
                ptr,
                e
            );
            -1
        }
    }
}
