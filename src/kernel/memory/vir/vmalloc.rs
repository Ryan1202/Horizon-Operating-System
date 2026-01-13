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
            FRAME_MANAGER, Frame, FrameAllocator, FrameData, FrameError, FrameNumber, FrameRange,
            FrameTag, ZoneType, buddy::FrameOrder,
        },
        vir::{
            page::{VirtPageNumber, VmRange},
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
        start: VirtPageNumber::from_addr(vstart).unwrap(),
        end: VirtPageNumber::from_addr(vstart + size - 1).unwrap(),
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
    let count =
        NonZeroUsize::new(size.div_ceil(PAGE_SIZE)).ok_or(MemoryError::InvalidSize(size))?;

    let frame = unsafe { Frame::from_addr(addr) };
    let frame_tag = frame.get_tag();
    match frame_tag {
        FrameTag::Unused => unsafe {
            frame.replace(
                FrameTag::Io,
                FrameData {
                    range: ManuallyDrop::new(FrameRange {
                        start: FrameNumber::from_addr(addr),
                        end: FrameNumber::from_addr(addr + count.get() * PAGE_SIZE - 1),
                    }),
                },
            );
        },
        _ => return Err(MemoryError::AddressConflict),
    }

    let node = get_vmap_node();
    let vpages = node.allocate(count)?;
    let vaddr = unsafe { vpages.as_ref() }.get_start_addr();
    unsafe {
        page_link(vaddr.get(), addr, count.get() as u16, cache_type);
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
            let start = VirtPageNumber::from_addr(vaddr).unwrap();
            let end = VirtPageNumber::new(NonZeroUsize::new(start.get().get() + count).unwrap());

            unsafe {
                frame.replace(FrameTag::Unused, FrameData { unused: () });

                page_unlink(vaddr, (count + 1) as u16);

                node.deallocate(&VmRange { start, end })
            }
        }
        _ => Err(MemoryError::PageError(FrameError::IncorrectFrameType)),
    }
}

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
    while left > 0 {
        let pages = FRAME_MANAGER.allocate_frames(ZoneType::HighMem, order);

        match pages {
            Some(frames) => {
                let paddr = frames.start_addr();

                match frames.get_tag() {
                    FrameTag::Unused => unsafe {
                        frames.replace(
                            FrameTag::Vmalloc,
                            FrameData {
                                vmalloc: ManuallyDrop::new((
                                    FrameRange {
                                        start: FrameNumber::from_addr(paddr),
                                        end: FrameNumber::from_addr(
                                            paddr + order.to_count() * PAGE_SIZE,
                                        ),
                                    },
                                    None,
                                )),
                            },
                        );
                    },
                    _ => return Err(MemoryError::AddressConflict),
                }
                unsafe {
                    page_link(vaddr, paddr, order.to_count() as u16, cache_type);
                }

                let this = Some(NonNull::from(frames));
                if let Some(mut last) = last_page {
                    let last = unsafe { last.as_mut() };

                    if let FrameTag::Vmalloc = last.get_tag() {
                        unsafe {
                            last.get_data_mut().vmalloc.1 = this;
                        }
                    }
                }
                last_page = this;
                vaddr += order.to_count() * PAGE_SIZE;
                left -= order.to_count();
            }
            None => {
                if order.val() == 0 {
                    vfree::<T>(VmRange {
                        start: VirtPageNumber::from_addr(vstart.get()).unwrap(),
                        end: VirtPageNumber::from_addr(vaddr).unwrap(),
                    })?;
                }
                order = order - 1;
            }
        }
    }

    Ok(vpages.with_addr(vstart).cast())
}

pub fn vfree<T>(range: VmRange) -> Result<(), MemoryError> {
    let node = get_vmap_node();

    let mut page = unsafe { Frame::from_addr(vir2phys(range.start.get().get())) };
    unsafe {
        while let FrameTag::Vmalloc = page.get_tag() {
            let (start, page_count, next) = {
                let vmalloc = &page.get_data_mut().vmalloc;
                let range = &vmalloc.0;
                (
                    range.start.get(),
                    range.end.get() - range.start.get() + 1,
                    vmalloc.1,
                )
            };
            page_unlink(start, page_count as u16);

            page = match next {
                Some(mut next_page) => next_page.as_mut(),
                None => break,
            }
        }
    }
    node.deallocate(&range)?;

    FRAME_MANAGER
        .free_frames(page)
        .map_err(|e| MemoryError::PageError(e))
}
