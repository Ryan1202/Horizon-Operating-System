use core::{mem, num::NonZeroUsize, ops::ControlFlow, sync::atomic::Ordering};

use crate::{
    arch::{ArchFlushTlb, ArchPageTable, PhysAddr},
    kernel::memory::{
        EARLY_ROOT_PT_VIR, KERNEL_BASE, KLINEAR_BASE, MemoryError, VMEMMAP_BASE,
        arch::ArchMemory,
        frame::{
            Frame, FrameError, FrameNumber, FrameTag, VMEMMAP_MAPPED_PAGES,
            buddy::FrameOrder,
            early::{LINEAR_END, VMAP_PER_PAGE, early_allocate_pages},
            options::FrameAllocOptions,
        },
        page::{
            FlushTlb, PageFlags, PageNumber, PageTableOps, current_root_pt,
            dyn_pages::DynPages,
            iter::{PageTableIter, PtStep},
            linear_table_ptr,
            lock::{NormalPtLock, PtPage},
        },
    },
};

fn metadata_range(start: FrameNumber, order: FrameOrder) -> (PageNumber, PageNumber) {
    let count = order.to_count().get();
    let start_addr = VMEMMAP_BASE + start.get() * size_of::<Frame>();
    let start = start_addr.to_page_number();
    let end = start + count;

    (start, end)
}

fn map_vmemmap_page(page: PageNumber) -> Result<(), MemoryError> {
    let (mut frame, _) = FrameAllocOptions::atomic(FrameOrder::new(0)).allocate()?;

    let paddr = PhysAddr::from_frame_number(frame.frame_number());
    let ptr = (KLINEAR_BASE + paddr.as_usize()).as_mut_ptr::<u8>();
    unsafe { ptr.write_bytes(0, ArchPageTable::PAGE_SIZE) };

    let mut pages = DynPages::fixed(page, const { NonZeroUsize::new(1).unwrap() });
    PageTableOps::map(
        current_root_pt(),
        &mut pages,
        0,
        &mut frame,
        PageFlags::new(),
    )?;
    mem::forget(frame);

    VMEMMAP_MAPPED_PAGES.fetch_add(1, Ordering::Relaxed);
    ArchFlushTlb::flush_page(page);
    Ok(())
}

pub(super) fn ensure_mapped(start: FrameNumber, order: FrameOrder) -> Result<(), MemoryError> {
    let root_pt = current_root_pt();
    let (page_start, page_end) = metadata_range(start, order);

    loop {
        let page = {
            let mut iter = PageTableIter::<ArchPageTable, NormalPtLock>::new(
                PtPage::from_ptr(root_pt as *const ArchPageTable),
                page_start,
                page_end,
                linear_table_ptr::<ArchPageTable>,
            )
            .ok_or(FrameError::Conflict)?;

            match iter.find_map(|x| match x {
                PtStep::Absent { page, .. } => Some(page),
                _ => None,
            }) {
                Some(page) => page,
                None => break,
            }
        };

        map_vmemmap_page(page)?;
    }

    Ok(())
}

fn get_order(frame_number: FrameNumber) -> Option<FrameNumber> {
    match Frame::get_tag_relaxed(frame_number) {
        FrameTag::Tail => {
            let head = unsafe { Frame::get_raw(frame_number).as_ref().get_data().range.start };
            get_order(head)
        }
        FrameTag::HardwareReserved | FrameTag::SystemReserved | FrameTag::BadMemory => {
            let end = unsafe { Frame::get_raw(frame_number).as_ref().get_data().range.end };
            Some(end)
        }
        FrameTag::Uninited => Some(frame_number + 1),
        _ => None,
    }
}

pub(super) fn conflict_check(start: FrameNumber, order: FrameOrder) -> bool {
    let root_pt = current_root_pt();
    let end = start + order.to_count().get();
    let (page_start, page_end) = metadata_range(start, order);

    let Some(iter) = PageTableIter::<ArchPageTable, NormalPtLock>::new(
        PtPage::from_ptr(root_pt as *const ArchPageTable),
        page_start,
        page_end,
        linear_table_ptr::<ArchPageTable>,
    ) else {
        return true;
    };

    let leaf_iter = iter.filter_map(|x| match x {
        PtStep::Leaf { page, .. } => Some(page),
        _ => None,
    });

    for page in leaf_iter {
        let num = page.get() - VMEMMAP_BASE.to_page_number().get();
        let range_start = FrameNumber::new(num);
        let range_end = range_start + VMAP_PER_PAGE;
        let overlap_start = start.max(range_start);
        let overlap_end = end.min(range_end);

        let mut frame_number = overlap_start;
        while frame_number < overlap_end {
            if let Some(end) = get_order(frame_number) {
                frame_number = end;
            } else {
                return true;
            }
        }
    }

    false
}

pub(super) fn early_create_vmemmap(
    block_start: FrameNumber,
    block_end: FrameNumber,
) -> ControlFlow<()> {
    let start = block_start.get() / VMAP_PER_PAGE;
    let end = block_end.get().div_ceil(VMAP_PER_PAGE);
    let count = end - start;
    let linear_end = LINEAR_END.load(Ordering::Relaxed);

    if block_end.get() > linear_end {
        let page = VMEMMAP_BASE.to_page_number() + start;

        let frame_start = early_allocate_pages(count).to_frame_number();
        let flags = PageFlags::new();

        // 清零
        let page_start = if (frame_start + count).get() < linear_end {
            KLINEAR_BASE + frame_start.get() * ArchPageTable::PAGE_SIZE
        } else {
            KERNEL_BASE + frame_start.get() * ArchPageTable::PAGE_SIZE
        };
        let ptr = page_start.as_mut_ptr::<u8>();
        unsafe { ptr.write_bytes(0, count * ArchPageTable::PAGE_SIZE) };

        let result = unsafe {
            PageTableOps::<ArchPageTable>::early_map(
                EARLY_ROOT_PT_VIR,
                page,
                frame_start,
                count,
                flags,
            )
        };

        if result.is_err() {
            printk!(
                "WARNING: early mapping failed for block {:#x} - {:#x}, skipping\n",
                block_start.get(),
                block_end.get()
            );
            return ControlFlow::Break(());
        }
    }
    ControlFlow::Continue(())
}
