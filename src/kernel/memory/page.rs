use core::{
    ffi::c_void,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ops::{Add, Sub},
};

use crate::{
    arch::{PhyAddr, VirtAddr, x86::kernel::page::PAGE_SIZE},
    kernel::memory::{
        KLINEAR_SIZE, MemoryError,
        frame::{
            Frame, FrameTag,
            buddy::FrameOrder,
            options::FrameAllocOptions,
            reference::{FrameMut, FrameRef},
        },
        page::{dyn_pages::DynPages, options::PageAllocOptions},
        vir_base_addr,
        vmalloc::vfree,
    },
};

pub(super) mod dyn_pages;
pub mod options;
pub(super) mod range;
pub(super) mod vmap;

pub trait PageTableEntry {
    const MAX: usize;

    fn new_absent() -> Self;

    fn new_present(phy_addr: PhyAddr) -> Self;

    fn is_present(&self) -> bool;

    fn phy_addr(&self) -> PhyAddr;
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
        VirtAddr::new(self.0.get() * PAGE_SIZE)
    }
}

impl Add<usize> for PageNumber {
    type Output = Self;

    fn add(self, rhs: usize) -> Self::Output {
        PageNumber(unsafe { NonZeroUsize::new_unchecked(self.0.get() + rhs) })
    }
}

impl Sub<usize> for PageNumber {
    type Output = Self;

    fn sub(self, rhs: usize) -> Self::Output {
        PageNumber(unsafe { NonZeroUsize::new_unchecked(self.0.get() - rhs) })
    }
}

pub enum Pages<'a> {
    Fixed((FrameMut, usize)),
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

    pub fn get_frame(&mut self) -> Option<&mut FrameMut> {
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

pub fn kernel_alloc_pages<'a>(count: NonZeroUsize) -> Result<Pages<'a>, MemoryError> {
    let order = FrameOrder::new(count.get().next_power_of_two().ilog2() as u8);

    let frame_options = FrameAllocOptions::new().dynamic(order);
    let page_options = PageAllocOptions::new(frame_options);

    let pages = page_options.allocate()?;

    Ok(pages)
}

#[unsafe(export_name = "kernel_alloc_pages")]
pub fn kernel_alloc_pages_c(count: usize) -> *mut core::ffi::c_void {
    let result = NonZeroUsize::new(count)
        .ok_or(MemoryError::InvalidSize(count))
        .and_then(|count| kernel_alloc_pages(count));

    match result {
        Ok(pages) => {
            let mut pages = ManuallyDrop::new(pages);
            pages.get_ptr()
        }
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling kernel_alloc_pages_c failed: count = {:#x}",
                count
            ));
            core::ptr::null_mut()
        }
    }
}

pub fn kernel_free_pages<T>(vaddr: VirtAddr) -> Result<(), MemoryError> {
    let linear_start = vir_base_addr();
    let linear_end = linear_start + KLINEAR_SIZE;

    // 如果在内核线性映射区，则无需释放虚拟页
    if vaddr >= linear_start && vaddr < linear_end {
        let phy_addr = PhyAddr::new(vaddr.offset_from(linear_start));
        let frame = Frame::get_raw(phy_addr.to_frame_number());

        match unsafe { frame.as_ref() }.get_tag() {
            FrameTag::Unused | FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                Err(MemoryError::MultipleFree)
            }
            FrameTag::Free => panic!(
                "Attempt to free frame with unavailable tag at vaddr: {:#x}",
                vaddr.as_usize()
            ),
            _ => unsafe {
                if FrameMut::try_from_raw(frame).is_none() {
                    let _ = FrameRef::from_raw(frame).ok_or(MemoryError::MultipleFree)?;
                }

                Ok(())
            },
        }
    } else {
        vfree(vaddr)
    }
}

#[unsafe(export_name = "kernel_free_pages")]
pub fn kernel_free_pages_c(ptr: usize) -> i32 {
    match kernel_free_pages::<c_void>(VirtAddr::new(ptr)) {
        Ok(_) => 0,
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling kernel_free_pages from C failed: vaddr = {:#x}",
                ptr
            ));
            -1
        }
    }
}
