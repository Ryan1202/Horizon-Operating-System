use core::{ffi::c_void, mem::transmute, num::NonZeroUsize, ptr::NonNull};

use crate::kernel::memory::phy::page::Page;

use super::slub::config::DEFAULT_CACHES;

#[unsafe(export_name = "kmalloc")]
pub extern "C" fn kmalloc_c(size: usize) -> *mut c_void {
    let size = match NonZeroUsize::new(size) {
        Some(size) => size,
        None => return core::ptr::null_mut(),
    };

    unsafe { transmute(kmalloc::<c_void>(size)) }
}

pub fn kmalloc<T>(size: NonZeroUsize) -> Option<NonNull<T>> {
    let ilog = size.get().next_power_of_two().ilog2().max(3) as usize;

    if ilog > 12 {
        // 超过 4096 bytes，暂不支持大对象分配
        return None;
    }

    let cache = unsafe { DEFAULT_CACHES[ilog - 3].as_mut() };
    cache.alloc::<T>()
}

#[unsafe(export_name = "kfree")]
pub extern "C" fn kfree_c(ptr: *mut c_void) {
    let ptr = match NonNull::new(ptr) {
        Some(ptr) => ptr,
        None => {
            return;
        }
    };
    kfree(ptr);
}

pub fn kfree<T>(ptr: NonNull<T>) {
    unsafe {
        let mut page = Page::from_addr(ptr.as_ptr() as usize);

        match page.as_mut() {
            Page::Slub(slub) => {
                slub.free(ptr.cast());
            }
            _ => {
                return;
            }
        }
    }
}
