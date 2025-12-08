use core::{
    ffi::c_void,
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use crate::kernel::memory::{
    MemoryError, PageCacheType, page_link, page_unlink,
    phy::page::{PAGE_SIZE, Page, PageAllocator, ZoneType, buddy::PageOrder, page_manager},
    vir::{
        page::{VirtPageNumber, VmRange},
        vmap::get_vmap_node,
    },
    vir2phys,
};

#[unsafe(no_mangle)]
pub extern "C" fn vmalloc_init() {
    get_vmap_node().init();
}

#[unsafe(export_name = "ioremap")]
pub extern "C" fn ioremap_c(
    addr: usize,
    size: usize,
    cache_type: PageCacheType,
) -> *mut core::ffi::c_void {
    match ioremap::<core::ffi::c_void>(addr, size, cache_type) {
        Ok(ptr) => ptr.as_ptr(),
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling ioremap from C failed: addr = {:#x}, size = {:#x}",
                addr, size
            ));
            null_mut()
        }
    }
}

#[unsafe(export_name = "iounmap")]
pub extern "C" fn iounmap_c(vstart: usize) -> i32 {
    let range = VmRange {
        start: VirtPageNumber::from_addr(vstart).unwrap(),
        end: VirtPageNumber::from_addr(vstart).unwrap(),
    };
    match iounmap::<core::ffi::c_void>(range) {
        Ok(_) => 0,
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling iounmap from C failed: vstart: {:#x}",
                vstart
            ));
            -1
        }
    }
}

#[unsafe(export_name = "vmalloc")]
pub extern "C" fn vmalloc_c(size: usize, cache_type: PageCacheType) -> *mut c_void {
    match vmalloc::<c_void>(size, cache_type) {
        Ok(ptr) => ptr.as_ptr(),
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling vmalloc from C failed: size = {:#x}",
                size
            ));
            null_mut()
        }
    }
}

#[unsafe(export_name = "vfree")]
pub extern "C" fn vfree_c(vstart: usize, size: usize) -> i32 {
    let range = VmRange {
        start: VirtPageNumber::from_addr(vstart).unwrap(),
        end: VirtPageNumber::from_addr(vstart + size - 1).unwrap(),
    };
    match vfree::<c_void>(range) {
        Ok(_) => 0,
        Err(e) => {
            e.log_error(format_args!(
                "WARNING: calling vfree from C failed: vstart = {:#x}, size = {:#x}",
                vstart, size
            ));
            -1
        }
    }
}

pub fn ioremap<T>(
    addr: usize,
    size: usize,
    cache_type: PageCacheType,
) -> Result<NonNull<T>, MemoryError> {
    let count =
        NonZeroUsize::new(size.div_ceil(PAGE_SIZE)).ok_or(MemoryError::InvalidSize(size))?;

    let node = get_vmap_node();
    let vpages = node.allocate(count)?;
    let vaddr = unsafe { vpages.as_ref() }.get_start_addr();

    unsafe {
        page_link(vaddr.get(), addr, count.get() as u16, cache_type);
    }

    Ok(vpages.with_addr(vaddr).cast())
}

pub fn iounmap<T>(range: VmRange) -> Result<(), MemoryError> {
    let node = get_vmap_node();
    unsafe {
        page_unlink(range.start.get().get(), range.get_count() as u16);

        node.deallocate(&range)
    }
}

pub fn vmalloc<T>(size: usize, cache_type: PageCacheType) -> Result<NonNull<T>, MemoryError> {
    let count =
        NonZeroUsize::new(size.div_ceil(PAGE_SIZE) | 1).ok_or(MemoryError::InvalidSize(size))?;

    let node = get_vmap_node();
    let vpages = node.allocate(count)?;
    let vaddr = unsafe { vpages.as_ref() }.get_start_addr();

    let pages = page_manager()
        .lock()
        .allocate_pages(ZoneType::HighMem, PageOrder::new(count.ilog2() as u8));

    match pages {
        Some(pages) => {
            let paddr = unsafe { pages.as_ref() }.start_addr();
            unsafe {
                page_link(vaddr.get(), paddr, count.get() as u16, cache_type);
            }
            Ok(vpages.with_addr(vaddr).cast())
        }
        None => {
            node.deallocate(unsafe { vpages.as_ref() }.rb_node.get_key())?;
            Err(MemoryError::OutOfMemory)
        }
    }
}

pub fn vfree<T>(range: VmRange) -> Result<(), MemoryError> {
    let node = get_vmap_node();

    let page = unsafe { Page::from_addr(vir2phys(range.start.get().get())) };

    unsafe {
        page_unlink(range.start.get().get(), range.get_count() as u16);

        node.deallocate(&range)?;
    }

    page_manager()
        .lock()
        .free_pages(page)
        .map_err(|e| MemoryError::PageError(e))
}
