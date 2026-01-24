use core::{
    ffi::c_void,
    mem::ManuallyDrop,
    num::NonZeroUsize,
    ptr::{NonNull, null_mut},
};

use crate::{
    arch::x86::kernel::page::PAGE_SIZE,
    kernel::memory::{
        MemoryError, PageCacheType, page_link, page_unlink,
        phy::frame::{
            Frame, FrameData, FrameError, FrameNumber, FrameRange, FrameTag, buddy::FrameOrder,
            frame_count, options::FrameAllocOptions, zone::ZoneType,
        },
        vir::{
            page::{PageNumber, VmRange},
            vmap::get_vmap_node,
        },
        vir2phys,
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
    match iounmap::<core::ffi::c_void>(vstart) {
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
        start: PageNumber::from_addr(vstart).unwrap(),
        end: PageNumber::from_addr(vstart + size - 1).unwrap(),
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
    let start = FrameNumber::from_addr(addr);
    let count = frame_count(size);
    let non_zero_count = NonZeroUsize::new(count).ok_or(MemoryError::InvalidSize(size))?;

    FrameAllocOptions::new()
        .fixed(start, non_zero_count)
        .allocate()
        .map_err(MemoryError::FrameError)?;

    let frame = unsafe { Frame::from_addr(addr) };
    match frame.get_tag() {
        FrameTag::Unused => unsafe {
            let end = start + count - 1;

            let range = ManuallyDrop::new(FrameRange { start, end });

            frame.replace(FrameTag::Io, FrameData { range });
        },
        _ => return Err(MemoryError::AddressConflict),
    }

    let node = get_vmap_node();
    let vpages = node.allocate(non_zero_count)?;
    let vaddr = unsafe { vpages.as_ref() }.get_start_addr();
    unsafe {
        page_link(vaddr.get(), addr, count as u16, cache_type);
    }

    Ok(vpages.with_addr(vaddr).cast())
}

pub fn iounmap<T>(vaddr: usize) -> Result<(), MemoryError> {
    let node = get_vmap_node();

    let frame = unsafe { Frame::from_addr(vir2phys(vaddr)) };
    let frame_tag = frame.get_tag();
    match frame_tag {
        FrameTag::Io => {
            let count = unsafe {
                let range = &mut frame.get_data_mut().range;
                range.end.get() - range.start.get()
            };
            let start = PageNumber::from_addr(vaddr).unwrap();
            let end = PageNumber::new(NonZeroUsize::new(start.get().get() + count).unwrap());

            unsafe {
                frame.replace(FrameTag::Unused, FrameData { unused: () });

                page_unlink(vaddr, (count + 1) as u16);

                node.deallocate(&VmRange { start, end })
            }
        }
        _ => Err(MemoryError::FrameError(FrameError::IncorrectFrameType)),
    }
}

static VMALLOC_FALLBACK_CHAIN: [ZoneType; 3] =
    [ZoneType::HighMem, ZoneType::LinearMem, ZoneType::MEM24];

pub fn vmalloc<T>(size: usize, cache_type: PageCacheType) -> Result<NonNull<T>, MemoryError> {
    let count =
        NonZeroUsize::new(size.div_ceil(PAGE_SIZE) | 1).ok_or(MemoryError::InvalidSize(size))?;

    let node = get_vmap_node();
    let vpages = node.allocate(count)?;
    let vstart = unsafe { vpages.as_ref() }.get_start_addr();

    let mut vaddr = vstart.get();
    let mut left = count.get();
    let mut order = FrameOrder::new(count.ilog2() as u8);
    let mut last_page: Option<NonNull<Frame>> = None;

    let frame_options = FrameAllocOptions::new()
        .fallback(&VMALLOC_FALLBACK_CHAIN)
        .dynamic(order);

    while left > 0 {
        let result = frame_options.allocate().map_err(MemoryError::FrameError);

        match result {
            Ok((frames, _)) => {
                let paddr = frames.start_addr();
                let count = order.to_count().get();

                match frames.get_tag() {
                    FrameTag::Unused => unsafe {
                        let start = FrameNumber::from_addr(paddr);
                        let end = start + count;
                        let range = FrameRange { start, end };
                        let vmalloc = ManuallyDrop::new((range, None));

                        frames.replace(FrameTag::Vmalloc, FrameData { vmalloc });
                    },
                    _ => return Err(MemoryError::AddressConflict),
                }

                unsafe { page_link(vaddr, paddr, count as u16, cache_type) };

                let this = Some(NonNull::from(frames));
                if let Some(mut last) = last_page {
                    let last = unsafe { last.as_mut() };

                    if let FrameTag::Vmalloc = last.get_tag() {
                        unsafe { last.get_data_mut().vmalloc.1 = this };
                    }
                }
                last_page = this;

                vaddr += count * PAGE_SIZE;
                left -= count;
            }
            Err(_) => {
                if order.get() == 0 {
                    let start = PageNumber::from_addr(vstart.get()).unwrap();
                    let end = PageNumber::from_addr(vaddr).unwrap();

                    vfree::<T>(VmRange { start, end })?;
                }
                order = order - 1;
            }
        }
    }

    Ok(vpages.with_addr(vstart).cast())
}

pub fn vfree<T>(range: VmRange) -> Result<(), MemoryError> {
    let node = get_vmap_node();

    let mut frame = unsafe { Frame::from_addr(vir2phys(range.start.get().get())) };
    unsafe {
        while let FrameTag::Vmalloc = frame.get_tag() {
            let (start, page_count, next) = {
                let vmalloc = &frame.get_data_mut().vmalloc;
                let range = &vmalloc.0;

                (
                    range.start.get(),
                    range.end.get() - range.start.get() + 1,
                    vmalloc.1,
                )
            };
            page_unlink(start, page_count as u16);

            frame = match next {
                Some(mut next_page) => next_page.as_mut(),
                None => break,
            }
        }
    }
    node.deallocate(&range)?;

    frame.free().map_err(MemoryError::FrameError)
}
