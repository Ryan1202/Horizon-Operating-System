use core::{
    ffi::c_void,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        MemoryError, PageCacheType,
        arch::ArchMemory,
        frame::{buddy::FrameOrder, frame_count, options::FrameAllocOptions, zone::ZoneType},
        page::{Pages, options::PageAllocOptions, range::VmRange, vmap::get_vmap},
    },
};

#[unsafe(no_mangle)]
pub extern "C" fn vmap_init() {
    get_vmap().init();
}

#[unsafe(export_name = "ioremap")]
pub extern "C" fn ioremap_c(
    addr: usize,
    size: usize,
    cache_type: PageCacheType,
) -> *mut core::ffi::c_void {
    match ioremap(PhysAddr::new(addr), size, cache_type) {
        Ok(mut ptr) => ptr.get_ptr().as_ptr(),
        Err(e) => {
            printk!(
                "WARNING: calling ioremap from C failed: addr = {:#x}, size = {:#x}, error = {:?}\n",
                addr,
                size,
                e
            );
            null_mut()
        }
    }
}

#[unsafe(export_name = "iounmap")]
pub extern "C" fn iounmap_c(vstart: usize) -> i32 {
    match vfree(VirtAddr::new(vstart)) {
        Ok(_) => 0,
        Err(e) => {
            printk!(
                "WARNING: calling iounmap from C failed: vstart: {:#x}, error = {:?}\n",
                vstart,
                e
            );
            -1
        }
    }
}

#[unsafe(export_name = "vmalloc")]
pub extern "C" fn vmalloc_c(size: usize, cache_type: PageCacheType) -> *mut c_void {
    let size = match NonZeroUsize::new(size) {
        Some(size) => size,
        None => {
            return null_mut();
        }
    };

    match vmalloc::<c_void>(size, cache_type) {
        Ok(ptr) => ptr.as_ptr(),
        Err(e) => {
            printk!(
                "WARNING: calling vmalloc from C failed: size = {:#x}, error = {:?}\n",
                size,
                e
            );
            null_mut()
        }
    }
}

pub fn ioremap<'a>(
    addr: PhysAddr,
    size: usize,
    cache_type: PageCacheType,
) -> Result<Pages<'a>, MemoryError> {
    let start = addr.to_frame_number();
    let count = frame_count(size);
    let non_zero_count = NonZeroUsize::new(count).ok_or(MemoryError::InvalidSize(size))?;

    let page_options = PageAllocOptions::mmio(start, non_zero_count, cache_type);

    page_options.allocate()
}

pub fn vmalloc<T>(
    size: NonZeroUsize,
    cache_type: PageCacheType,
) -> Result<NonNull<T>, MemoryError> {
    let count = NonZeroUsize::new(size.get().div_ceil(ArchPageTable::PAGE_SIZE))
        .ok_or(MemoryError::InvalidSize(size.get()))?;

    let order = FrameOrder::new(count.ilog2() as u8);

    let frame_options = FrameAllocOptions::new()
        .fallback(&[ZoneType::MEM32])
        .dynamic(order);

    let page_options = PageAllocOptions::new(frame_options)
        .contiguous(false)
        .cache_type(cache_type);

    page_options
        .allocate()
        .map(ManuallyDrop::new)
        .map(|mut pages| pages.get_ptr())
}

pub fn vfree(vaddr: VirtAddr) -> Result<(), MemoryError> {
    let err = MemoryError::InvalidAddress(vaddr);

    let num = vaddr.to_page_number();
    let range = VmRange {
        start: num,
        end: num,
    };

    let mut node = get_vmap();
    let pages = unsafe { node.search_allocated(&range).ok_or(err)?.as_mut() };

    pages.unmap()?;

    node.deallocate(pages)
}
