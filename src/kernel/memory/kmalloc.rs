use core::{ffi::c_void, mem::transmute, ptr::NonNull};

use crate::kernel::memory::page::Page;

use super::slub::config::DEFAULT_CACHES;

#[unsafe(no_mangle)]
pub extern "C" fn kmalloc(size: usize) -> *mut c_void {
    if size == 0 {
        return core::ptr::null_mut();
    }
    let ilog = size.next_power_of_two().ilog2().max(3) as usize;

    if ilog > 12 {
        // 超过 4096 bytes，暂不支持大对象分配
        return core::ptr::null_mut();
    }

    unsafe {
        let cache = DEFAULT_CACHES[ilog - 3].as_mut();
        transmute(cache.alloc::<c_void>())
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn kfree(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }

    unsafe {
        let mut page = Page::from_addr(ptr as usize);

        match page.as_mut() {
            Page::Slub(slub) => {
                slub.free(NonNull::new(ptr).unwrap().cast());
            }
            _ => {
                return;
            }
        }
    }
}
