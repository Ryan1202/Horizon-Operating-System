use core::{
    ffi::c_void,
    mem::transmute,
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        KLINEAR_SIZE, MemoryError,
        arch::ArchMemory,
        frame::{Frame, FrameTag, buddy::FrameOrder},
        page::{kfree_pages, options::PageAllocOptions},
        slub::{Slub, config::select_cache},
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

    unsafe { transmute(kzalloc::<c_void>(size)) }
}

pub fn kmalloc<T>(size: NonZeroUsize) -> Option<NonNull<T>> {
    match select_cache(size) {
        Some(cache) => cache.allocate(),
        _ => {
            let ilog = size.get().next_power_of_two().ilog2() as usize;
            let order = FrameOrder::new((ilog - ArchPageTable::PAGE_BITS) as u8);

            let page_options = PageAllocOptions::kernel(order);
            let mut pages = page_options.allocate().ok()?;

            Some(unsafe { NonNull::new_unchecked(pages.get_ptr()) })
        }
    }
}

pub fn kzalloc<T>(size: NonZeroUsize) -> Option<NonNull<T>> {
    let ptr = kmalloc(size)?;
    unsafe { ptr.cast::<u8>().as_ptr().write_bytes(0, size.get()) };
    Some(ptr)
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
    let addr = ptr.as_ptr() as usize;
    assert!(
        vir_base_addr().as_usize() <= addr && addr <= vir_base_addr().as_usize() + KLINEAR_SIZE,
        "Attempt to free non-kernel memory"
    );

    let vaddr = VirtAddr::new(addr);
    let phy_addr = PhysAddr::new(vaddr.offset_from(vir_base_addr()));
    let frame_number = phy_addr.to_frame_number();

    match Frame::get_tag_relaxed(frame_number) {
        FrameTag::Slub => {
            let frame = unsafe { Frame::get_raw(frame_number).as_mut() };
            let slub: &mut Slub = frame.try_into().unwrap();
            slub.deallocate(ptr.cast());
            Ok(())
        }
        FrameTag::Tail => {
            let head = unsafe { Frame::get_raw(frame_number).as_ref().get_data().range.start };
            let head_frame = unsafe { Frame::get_raw(head).as_mut() };
            let slub: &mut Slub = head_frame.try_into().unwrap();
            slub.deallocate(ptr.cast());
            Ok(())
        }
        FrameTag::Anonymous => kfree_pages(vaddr),
        _ => Err(MemoryError::InvalidAddress(vaddr)),
    }
}
