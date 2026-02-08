use core::{ffi::c_void, mem::ManuallyDrop};

use crate::kernel::memory::{
    MemoryError,
    phy::frame::{buddy::FrameOrder, options::FrameAllocOptions},
    vir::{
        page::{Pages, options::PageAllocOptions},
        vmalloc::vfree,
    },
};

pub trait PageTableEntry {
    type PhyAddr;

    const MAX: usize;

    fn new_absent() -> Self;

    fn new_present(phy_addr: Self::PhyAddr) -> Self;

    fn is_present(&self) -> bool;

    fn phy_addr(&self) -> Self::PhyAddr;
}

pub fn kernel_alloc_pages<'a>(count: usize) -> Result<Pages<'a>, MemoryError> {
    let order = FrameOrder::new(count.next_power_of_two().ilog2() as u8);

    let frame_options = FrameAllocOptions::new().dynamic(order);
    let page_options = PageAllocOptions::new(frame_options);

    let pages = page_options.allocate()?;

    Ok(pages)
}

#[unsafe(export_name = "kernel_alloc_pages")]
pub fn kernel_alloc_pages_c(count: usize) -> *mut core::ffi::c_void {
    match kernel_alloc_pages(count) {
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

#[unsafe(export_name = "kernel_free_pages")]
pub fn kernel_free_pages_c(ptr: usize) -> i32 {
    match vfree::<c_void>(ptr) {
        Ok(_) => 0,
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling vfree from C failed: vaddr = {:#x}",
                ptr
            ));
            -1
        }
    }
}
