use core::{
    ffi::c_void,
    mem::transmute,
    num::NonZeroUsize,
    ops::DerefMut,
    ptr::{NonNull, null_mut},
};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        MemoryError,
        arch::ArchMemory,
        frame::{Frame, FrameTag, buddy::FrameOrder},
        page::{kfree_pages, options::PageAllocOptions},
        slub::{Slub, config::get_cache_unchecked},
        vir_base_addr,
    },
};

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
        None => null_mut(),
    }
}

pub fn kmalloc<T>(size: NonZeroUsize) -> Option<NonNull<T>> {
    let ilog = size.get().max(8).next_power_of_two().ilog2() as usize;

    if ilog > 12 {
        let order = FrameOrder::from_frame_count(ilog - ArchPageTable::PAGE_BITS);

        let page_options = PageAllocOptions::kernel(order);
        let mut pages = page_options.allocate().ok()?;

        Some(unsafe { NonNull::new_unchecked(pages.get_ptr()) })
    } else {
        let cache = unsafe { get_cache_unchecked(ilog - 3).clone().as_mut() };
        cache.alloc::<T>()
    }
}

#[unsafe(export_name = "kfree")]
pub extern "C" fn kfree_c(ptr: *mut c_void) {
    let ptr = match NonNull::new(ptr) {
        Some(ptr) => ptr,
        None => {
            return;
        }
    };
    let _ = kfree(ptr);
}

pub fn kfree<T>(ptr: NonNull<T>) -> Result<(), MemoryError> {
    assert!(
        ptr.as_ptr() as usize >= vir_base_addr().as_usize(),
        "Attempt to free non-kernel memory"
    );

    let vaddr = VirtAddr::new(ptr.as_ptr() as usize);
    let phy_addr = PhysAddr::new(vaddr.offset_from(vir_base_addr()));
    let mut frame = Frame::from_addr_mut(phy_addr).ok_or(MemoryError::InvalidAddress(vaddr))?;

    match frame.get_tag() {
        FrameTag::Slub => {
            let slub: &mut Slub = frame.deref_mut().try_into().unwrap();
            slub.free(ptr.cast())
        }
        FrameTag::Allocated => kfree_pages(vaddr),
        _ => Err(MemoryError::InvalidAddress(vaddr)),
    }
}
