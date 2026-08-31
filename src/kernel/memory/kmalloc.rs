use core::{
    alloc::{AllocError, Allocator, GlobalAlloc, Layout},
    ffi::c_void,
    marker::PhantomData,
    mem::{ManuallyDrop, transmute},
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use alloc::alloc::{AllocatorClone, StaticAllocator};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        KLINEAR_BASE, KLINEAR_END, MemoryError,
        arch::ArchMemory,
        frame::{Frame, FrameTag, buddy::FrameOrder},
        page::{kfree_pages, options::PageAllocOptions},
        slub::{Slub, config::select_cache},
    },
};

/// `PanicAllocator` 是一个在分配时直接触发 panic 的假分配器，用来当作全局分配器的占位符以通过编译，防止在内核初始化之前使用全局分配器
#[global_allocator]
static PANIC_ALLOCATOR: PanicAllocator = PanicAllocator;

struct PanicAllocator;

/// `Kmalloc` 为该类型实现了不会陷入等待的内核堆分配器
#[derive(Clone)]
pub struct Atomic;

/// `Kmalloc` 为该类型实现了可能陷入等待的内核堆分配器
#[derive(Clone)]
pub struct Kernel;

/// 内核堆分配器
///
/// `T` 是分配器的类型参数，表示分配器的行为特性。`Atomic` 表示不会等待的分配器，适用于内核中需要保证原子性的场景。
#[derive(Clone, Copy)]
pub struct Kmalloc<T = Kernel> {
    _marker: PhantomData<T>,
}

impl<T> Kmalloc<T> {
    pub const fn new() -> Self {
        Kmalloc {
            _marker: PhantomData,
        }
    }
}

const impl<T> Default for Kmalloc<T> {
    fn default() -> Self {
        Self::new()
    }
}

unsafe impl GlobalAlloc for PanicAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        panic!(
            "Attempted to allocate memory before kernel heap initialization: size = {}, align = {}",
            layout.size(),
            layout.align()
        );
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        panic!(
            "Attempted to deallocate memory before kernel heap initialization: ptr = {:p}, size = {}, align = {}",
            ptr,
            layout.size(),
            layout.align()
        );
    }
}

unsafe impl Allocator for Kmalloc<Atomic> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        let size = match NonZeroUsize::new(layout.size()) {
            Some(size) => size,
            None => return Err(AllocError),
        };

        let ptr = kmalloc(size).ok_or(AllocError)?;
        Ok(NonNull::slice_from_raw_parts(ptr, layout.size()))
    }

    unsafe fn deallocate(&self, ptr: NonNull<u8>, layout: Layout) {
        if let Err(e) = kfree(ptr) {
            printk!(
                "WARNING: Failed to free memory at {:p} of size {}: {:?}",
                ptr.as_ptr(),
                layout.size(),
                e
            );
        }
    }
}

unsafe impl Allocator for Kmalloc<Kernel> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        let size = match NonZeroUsize::new(layout.size()) {
            Some(size) => size,
            None => return Err(AllocError),
        };

        let ptr = kmalloc(size).ok_or(AllocError)?;
        Ok(NonNull::slice_from_raw_parts(ptr, layout.size()))
    }

    unsafe fn deallocate(&self, ptr: NonNull<u8>, layout: Layout) {
        if let Err(e) = kfree(ptr) {
            printk!(
                "WARNING: Failed to free memory at {:p} of size {}: {:?}",
                ptr.as_ptr(),
                layout.size(),
                e
            );
        }
    }
}

unsafe impl<T> AllocatorClone for Kmalloc<T> where Kmalloc<T>: Allocator + Clone {}
unsafe impl<T> StaticAllocator for Kmalloc<T> where Kmalloc<T>: Allocator + Clone {}

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
            let pages = ManuallyDrop::new(page_options.allocate().ok()?);

            Some(pages.get_ptr())
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
            printk!("WARNING: Attempt to free a null pointer\n");
            return;
        }
    };
    let _ = kfree(ptr);
}

pub fn kfree<T>(ptr: NonNull<T>) -> Result<(), MemoryError> {
    let addr = ptr.as_ptr() as usize;
    assert!(
        KLINEAR_BASE.as_usize() <= addr && addr <= KLINEAR_END.as_usize(),
        "Attempt to free non-kernel memory"
    );

    let vaddr = VirtAddr::new(addr);
    let phy_addr = PhysAddr::new(vaddr.offset_from(KLINEAR_BASE).unwrap());
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
        _ => Err(MemoryError::InvalidVirtualAddress(vaddr)),
    }
}
