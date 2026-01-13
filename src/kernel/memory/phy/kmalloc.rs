use core::{
    ffi::c_void,
    mem::transmute,
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use crate::kernel::memory::{
    VIR_BASE_ADDR,
    phy::{
        page::{Frame, FrameTag},
        slub::Slub,
    },
};

use super::slub::config::DEFAULT_CACHES;

#[unsafe(export_name = "kmalloc")]
pub extern "C" fn kmalloc_c(size: usize) -> *mut c_void {
    let size = match NonZeroUsize::new(size) {
        Some(size) => size,
        None => return null_mut(),
    };

    unsafe { transmute(kmalloc::<c_void>(size)) }
}

#[unsafe(export_name = "kzalloc")]
pub extern "C" fn kzalloc_c(size: usize) -> *mut c_void {
    let size = match NonZeroUsize::new(size) {
        Some(size) => size,
        None => return null_mut(),
    };

    match kmalloc::<c_void>(size) {
        Some(ptr) => {
            unsafe { ptr.write_bytes(0, size.get()) };
            ptr.as_ptr()
        }
        None => return null_mut(),
    }
}

pub fn kmalloc<T>(size: NonZeroUsize) -> Option<NonNull<T>> {
    let ilog = size.get().max(8).next_power_of_two().ilog2() as usize;

    if ilog > 12 {
        // 超过 4096 bytes，暂不支持大对象分配
        return None;
    }

    #[allow(static_mut_refs)]
    let cache = unsafe { DEFAULT_CACHES.get_unchecked(ilog - 3).clone().as_mut() };
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
        let phy_addr = ptr.as_ptr() as usize - VIR_BASE_ADDR;
        let page = Frame::from_addr(phy_addr);

        if let FrameTag::Slub = *page.tag.get() {
            Slub::from_frame(page).free(ptr.cast());
        } else {
            // 非 Slub 分配的内存，不支持释放
            panic!("Attempt to free non-Slub allocated memory");
        }
    }
}
