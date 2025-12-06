use core::{num::NonZeroUsize, ptr::NonNull};

use crate::kernel::memory::{
    PageCacheType, page_link, phy::page::PAGE_SIZE, vir::vmap::get_vmap_node,
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
        Some(ptr) => ptr.as_ptr(),
        None => core::ptr::null_mut(),
    }
}

pub fn ioremap<T>(addr: usize, size: usize, cache_type: PageCacheType) -> Option<NonNull<T>> {
    let count = NonZeroUsize::new(size.div_ceil(PAGE_SIZE))?;

    let node = get_vmap_node();
    let vpages = node.allocate(count)?;
    let vaddr = unsafe { vpages.as_ref() }.get_start_addr();

    unsafe {
        page_link(vaddr.get(), addr, count.get() as u16, cache_type);
    }

    Some(vpages.with_addr(vaddr).cast())
}

pub fn vmalloc<T>(size: usize) -> Option<NonNull<T>> {
    let count = NonZeroUsize::new(size.div_ceil(PAGE_SIZE))?;

    // let node = get_vmap_node();
    // let vaddr = node.allocate(count).map(|vpages| {
    //     let start_addr = unsafe { vpages.as_ref() }.get_start_addr();
    //     NonNull::dangling().with_addr(start_addr)
    // });

    unimplemented!()
}
