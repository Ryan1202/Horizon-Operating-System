use core::{
    ffi::c_void,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use crate::{
    arch::x86::kernel::page::PAGE_SIZE,
    kernel::memory::{
        KLINEAR_SIZE, MemoryError, PageCacheType,
        phy::frame::{
            Frame, FrameNumber, FrameTag,
            buddy::FrameOrder,
            frame_count,
            options::FrameAllocOptions,
            reference::{FrameMut, FrameRef},
            zone::ZoneType,
        },
        vir::{
            page::{PageNumber, Pages, VmRange, options::PageAllocOptions},
            vmap::get_vmap_node,
        },
        vir_base_addr,
    },
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
    match ioremap(addr, size, cache_type) {
        Ok(mut ptr) => ptr.get_ptr(),
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
    match iounmap(vstart) {
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
    let size = match NonZeroUsize::new(size) {
        Some(size) => size,
        None => {
            return null_mut();
        }
    };

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

pub fn ioremap<'a>(
    addr: usize,
    size: usize,
    cache_type: PageCacheType,
) -> Result<Pages<'a>, MemoryError> {
    let start = FrameNumber::from_addr(addr);
    let count = frame_count(size);
    let non_zero_count = NonZeroUsize::new(count).ok_or(MemoryError::InvalidSize(size))?;

    let frame_options = FrameAllocOptions::new().fixed(start, non_zero_count);
    let page_options = PageAllocOptions::new(frame_options)
        .contiguous(true)
        .cache_type(cache_type);

    page_options.allocate()
}

pub fn iounmap(vaddr: usize) -> Result<(), MemoryError> {
    let node = get_vmap_node();

    let err = MemoryError::InvalidAddress(vaddr);
    let page_number = PageNumber::from_addr(vaddr).ok_or(err.clone())?;

    // Vmap的树只使用range.start进行比较，因此只需传入单页范围即可
    let range = VmRange {
        start: page_number,
        end: page_number,
    };
    let pages = unsafe { node.search_allocated(&range).ok_or(err)?.as_mut() };

    pages.unlink();

    node.deallocate(pages)
}

static VMALLOC_FALLBACK_CHAIN: [ZoneType; 3] =
    [ZoneType::HighMem, ZoneType::LinearMem, ZoneType::MEM24];

pub fn vmalloc<T>(
    size: NonZeroUsize,
    cache_type: PageCacheType,
) -> Result<NonNull<T>, MemoryError> {
    let count = NonZeroUsize::new(size.get().div_ceil(PAGE_SIZE))
        .ok_or(MemoryError::InvalidSize(size.get()))?;

    let order = FrameOrder::new(count.ilog2() as u8);

    let frame_options = FrameAllocOptions::new()
        .fallback(&VMALLOC_FALLBACK_CHAIN)
        .dynamic(order);

    let page_options = PageAllocOptions::new(frame_options)
        .contiguous(false)
        .cache_type(cache_type);

    page_options
        .allocate()
        .map(|pages| ManuallyDrop::new(pages))
        .map(|mut pages| unsafe { NonNull::new_unchecked(pages.get_ptr()) })
}

pub fn vfree<T>(vaddr: usize) -> Result<(), MemoryError> {
    let node = get_vmap_node();

    let err = MemoryError::InvalidAddress(vaddr);

    let linear_start = vir_base_addr();
    let linear_end = linear_start + KLINEAR_SIZE;

    // 如果在内核线性映射区，则无需释放虚拟页
    if vaddr >= linear_start && vaddr < linear_end {
        let frame = Frame::get_raw(FrameNumber::from_addr(vaddr - linear_start));
        match unsafe { frame.as_ref() }.get_tag() {
            FrameTag::Unused | FrameTag::HardwareReserved | FrameTag::SystemReserved => {
                Err(MemoryError::MultipleFree)
            }
            FrameTag::Free => panic!(
                "Attempt to free frame with unavailable tag at vaddr: {:#x}",
                vaddr
            ),
            _ => unsafe {
                if FrameMut::try_from_raw(frame).is_none() {
                    let _ = FrameRef::from_raw(frame).ok_or(MemoryError::MultipleFree)?;
                }

                Ok(())
            },
        }
    } else {
        let num = PageNumber::from_addr(vaddr).unwrap();
        let range = VmRange {
            start: num,
            end: num,
        };
        let pages = unsafe { node.search_allocated(&range).ok_or(err)?.as_mut() };

        pages.unlink();

        node.deallocate(pages)
    }
}
