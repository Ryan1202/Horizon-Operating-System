use core::{
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ops::{Add, AddAssign, Sub},
    ptr::NonNull,
};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        KLINEAR_BASE, KLINEAR_SIZE, MemoryError,
        arch::ArchMemory,
        frame::{
            Frame, FrameTag,
            buddy::FrameOrder,
            reference::{SharedFrames, UniqueFrames},
        },
        page::{dyn_pages::DynPages, options::PageAllocOptions},
        vmalloc::vfree,
    },
};

pub mod dyn_pages;
mod flags;
pub mod iter;
pub mod lock;
pub mod options;
pub(super) mod range;
pub mod table;
mod tlb;
pub(super) mod vmap;

pub use flags::PageFlags;
pub use table::{
    MappingChunk, PageTable, PageTableEntry, PageEntrySlot, PageTableError, PageTableOps,
    kernel_table_ptr, linear_table_ptr,
};
pub use tlb::FlushTlb;

unsafe extern "C" {
    unsafe fn read_cr3() -> usize;
}

pub(super) fn current_root_pt() -> *const usize {
    unsafe { (KLINEAR_BASE + read_cr3()).page_align_down().as_ptr() }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(transparent)]
pub struct PageNumber(usize);

impl PageNumber {
    pub const fn new(num: usize) -> Self {
        PageNumber(num)
    }

    pub const fn get(&self) -> usize {
        self.0
    }

    pub const fn to_addr(&self) -> VirtAddr {
        VirtAddr::new(self.0 * ArchPageTable::PAGE_SIZE)
    }
}

impl const AddAssign<usize> for PageNumber {
    fn add_assign(&mut self, rhs: usize) {
        self.0 = self
            .0
            .checked_add(rhs)
            .expect("PageNumber addition overflow");
    }
}

impl const Add<usize> for PageNumber {
    type Output = Self;

    fn add(self, rhs: usize) -> Self::Output {
        PageNumber(
            self.0
                .checked_add(rhs)
                .expect("PageNumber addition overflow"),
        )
    }
}

impl const Sub<usize> for PageNumber {
    type Output = Self;

    fn sub(self, rhs: usize) -> Self::Output {
        PageNumber(self.0 - rhs)
    }
}

pub enum Pages<'a> {
    Linear(ManuallyDrop<UniqueFrames>),
    Dynamic(&'a mut DynPages),
}

impl<'a> Pages<'a> {
    pub fn start_addr(&self) -> VirtAddr {
        match self {
            Pages::Linear(frame) => KLINEAR_BASE + frame.start_addr().as_usize(),
            Pages::Dynamic(vpages) => vpages.start_addr(),
        }
    }

    pub fn get_ptr<T>(&mut self) -> NonNull<T> {
        NonNull::new(self.start_addr().as_mut_ptr()).unwrap()
    }

    pub fn get_frame(&mut self) -> Option<&mut UniqueFrames> {
        match self {
            Pages::Linear(frame) => Some(frame),
            Pages::Dynamic(_) => None,
        }
    }

    pub fn into_frame(self) -> Option<UniqueFrames> {
        match self {
            Pages::Linear(frame) => Some(ManuallyDrop::into_inner(frame)),
            Pages::Dynamic(_) => None,
        }
    }

    pub fn get_count(&self) -> usize {
        match self {
            Pages::Linear(frame) => frame.order().to_count().get(),
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

#[unsafe(export_name = "vir2phy")]
pub extern "C" fn vir2phy_c(vaddr: usize) -> usize {
    let vaddr = VirtAddr::new(vaddr);

    match PageTableOps::<ArchPageTable>::translate(current_root_pt(), vaddr) {
        Some(paddr) => paddr.as_usize(),
        None => {
            printk!(
                "WARNING: vir2phy failed: Entry Absent! vaddr = {:#x}\n",
                vaddr.as_usize()
            );
            0
        }
    }
}

#[unsafe(export_name = "kmalloc_pages")]
pub fn kmalloc_pages_c(count: usize) -> *mut core::ffi::c_void {
    let result = NonZeroUsize::new(count)
        .ok_or(MemoryError::InvalidSize(count))
        .and_then(kmalloc_pages);

    match result {
        Ok(pages) => {
            let mut pages = ManuallyDrop::new(pages);
            pages.get_ptr().as_ptr()
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
    // 如果在内核线性映射区，则无需释放虚拟页
    if vaddr >= KLINEAR_BASE && vaddr < KLINEAR_BASE + KLINEAR_SIZE {
        let phy_addr = PhysAddr::new(vaddr.offset_from(KLINEAR_BASE));
        let frame_number = phy_addr.to_frame_number();
        let frame = Frame::get_raw(frame_number);

        let tag = Frame::get_tag_relaxed(frame_number);
        match tag {
            FrameTag::Uninited
            | FrameTag::HardwareReserved
            | FrameTag::SystemReserved
            | FrameTag::AssignedFixed
            | FrameTag::Tail
            | FrameTag::Free => {
                printk!(
                    "Trying to free unavailable frame! vaddr: {:#x}, tag: {:?}",
                    vaddr.as_usize(),
                    tag
                );
                Err(MemoryError::UnavailableFrame)
            }
            _ => unsafe {
                if let Some(unique) = UniqueFrames::try_from_raw(frame) {
                    drop(unique);
                } else if let Some(shared) = SharedFrames::from_raw(frame) {
                    drop(shared);
                } else {
                    printk!(
                        "Trying to free frame without ownership path! vaddr: {:#x}, tag: {:?}",
                        vaddr.as_usize(),
                        tag
                    );
                    return Err(MemoryError::UnavailableFrame);
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
