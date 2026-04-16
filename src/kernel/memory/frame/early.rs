use core::ops::ControlFlow;
use core::sync::atomic::Ordering;

use crate::kernel::memory::{EARLY_ROOT_PT_VIR, KERNEL_END, KLINEAR_END};
use crate::{
    arch::{ArchPageTable, PhysAddr},
    kernel::memory::{
        EARLY_ROOT_PT_PHY, KERNEL_BASE, KLINEAR_BASE, VMEMMAP_BASE,
        arch::ArchMemory,
        frame::zone::RESERVED_END,
        page::{
            PageFlags, PageNumber, PageTableOps,
            iter::{PtIter, PtStep},
            lock::{EarlyPtLock, PtPage},
        },
    },
};

use super::{
    ALLOCATED_PAGES, FRAME_MANAGER, Frame, FrameData, FrameNumber, FrameRange, FrameTag,
    PREALLOCATED_END_PHY, PREALLOCATED_START_PHY, TOTAL_PAGES, frame_count,
};

#[repr(C, packed)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
    reserved: u32,
}

#[unsafe(no_mangle)]
pub extern "C" fn page_early_init(kernel_end: usize) {
    unsafe {
        // 都向后对齐到页
        *PREALLOCATED_END_PHY.get() = PhysAddr::new(kernel_end)
            .max(*PREALLOCATED_END_PHY.get())
            .page_align_up();
        *PREALLOCATED_START_PHY.get() = *PREALLOCATED_END_PHY.get();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn page_init(blocks: *const E820Ards, block_count: u16, kernel_start: usize) {
    let kernel_range = (PhysAddr::new(kernel_start), unsafe {
        *PREALLOCATED_END_PHY.get()
    });
    Frame::init(blocks, block_count, kernel_range);

    FRAME_MANAGER.init();
}

// 内核启动早期分配的页都是不会释放的，如页表结构等
pub fn early_allocate_pages(count: usize) -> PhysAddr {
    unsafe {
        let addr = *PREALLOCATED_END_PHY.get();
        *PREALLOCATED_END_PHY.get() += count * ArchPageTable::PAGE_SIZE;
        ALLOCATED_PAGES.fetch_add(count, Ordering::Relaxed);

        addr
    }
}

pub(super) const VMAP_PER_PAGE: usize = ArchPageTable::PAGE_SIZE / size_of::<Frame>();

/// 填充 [start, end) 范围的 `Frame`
///
/// 实际上只填写 start 一个 `Frame`
fn fill_range(start: FrameNumber, end: FrameNumber, linear_end: &mut FrameNumber, e820_type: u32) {
    debug_assert!(start < end);

    let frame = unsafe { Frame::get_raw(start).as_mut() };
    let mut range = FrameRange { start, end };
    let mut data = FrameData { range };

    let tag = match e820_type {
        0 => FrameTag::SystemReserved,
        1 => FrameTag::Free,
        2 | 3 | 4 => FrameTag::HardwareReserved,
        _ => FrameTag::BadMemory,
    };

    let count = end.count_from(start);
    let mut flags = PageFlags::new();
    const KLINEAR_BASE_PAGE: PageNumber = KLINEAR_BASE.to_page_number();
    let page = PageNumber::new((KLINEAR_BASE_PAGE + range.start.get()).get());

    match tag {
        FrameTag::Free => {
            TOTAL_PAGES.fetch_add(count, Ordering::Relaxed);
        }
        FrameTag::SystemReserved => {
            TOTAL_PAGES.fetch_add(count, Ordering::Relaxed);
            ALLOCATED_PAGES.fetch_add(count, Ordering::Relaxed);
            flags = flags.writable(false);
        }
        _ => {
            return;
        }
    }

    unsafe {
        let result = PageTableOps::<ArchPageTable>::early_map(
            EARLY_ROOT_PT_VIR,
            page,
            start,
            count,
            *linear_end,
            flags,
            false,
        );

        if result.is_err() {
            return;
        }
        *linear_end = *linear_end + count;
    };

    if range.end < RESERVED_END {
        return;
    } else if range.start < RESERVED_END {
        range.start = RESERVED_END;
        data = FrameData { range };
    }

    unsafe { frame.replace(tag, data) };
}

/// 启动阶段的初始化函数，仅可在单线程环境使用
impl Frame {
    /// 初始化页结构体数组，根据E820内存块信息划分内存，返回页结构体数组所需的总字节数
    fn init(blocks: *const E820Ards, block_count: u16, kernel_range: (PhysAddr, PhysAddr)) {
        let (kernel_start, kernel_end) = (
            kernel_range.0.to_frame_number(),
            kernel_range.1.to_frame_number(),
        );

        let mut linear_end = FrameNumber::new(0);

        // 将每个可用内存块按Buddy的方式分割成块
        for i in 0..block_count {
            let block = unsafe { &*blocks.add(i as usize) };

            // 起始地址向后对齐，避免向前越界
            let start_addr = PhysAddr::new(block.base_addr as usize);

            let start_addr = start_addr.page_align_up();
            let block_start = start_addr.to_frame_number();

            let length = block.length as usize - start_addr.page_offset();

            let block_end =
                PhysAddr::new(block.base_addr as usize).to_frame_number() + frame_count(length);

            if block.block_type != 1 {
                continue;
            }

            if let ControlFlow::Break(_) = create_vmemmap(linear_end, block_start, block_end) {
                continue;
            }

            if block_start >= block_end {
                continue;
            }

            // 填充 [block_start, block_end) 范围
            if block_end <= kernel_start || block_start >= kernel_end {
                fill_range(block_start, block_end, &mut linear_end, block.block_type);
            } else {
                // 内存块和内核有重叠部分
                let e820_type = block.block_type;

                // 前半部分可用
                if block_start < kernel_start {
                    fill_range(block_start, kernel_start, &mut linear_end, e820_type);
                }

                fill_range(kernel_start, kernel_end, &mut linear_end, 0);

                // 后半部分可用
                if block_end > kernel_end {
                    fill_range(kernel_end, block_end, &mut linear_end, e820_type);
                }
            }
        }

        init_early_page_table_frames();
    }
}

fn init_early_page_table_frames() {
    Frame::init_page_table_frame(EARLY_ROOT_PT_PHY.to_frame_number());

    let root = PtPage::new(
        EARLY_ROOT_PT_VIR as *const ArchPageTable,
        EARLY_ROOT_PT_PHY.to_frame_number(),
    );

    for range in [
        (PageNumber::new(0), PageNumber::new(0x200)),
        (KLINEAR_BASE.to_page_number(), KLINEAR_END.to_page_number()),
        // (VMALLOC_BASE.to_page_number(), VMALLOC_END.to_page_number()),
        (VMEMMAP_BASE.to_page_number(), VMEMMAP_BASE.to_page_number()),
        (KERNEL_BASE.to_page_number(), KERNEL_END.to_page_number()),
    ] {
        let Some(iter) = PtIter::<ArchPageTable, EarlyPtLock>::new(
            root,
            range.0,
            range.1,
            ArchPageTable::kernel_table_ptr,
        ) else {
            panic!(
                "failed to create page table iterator for range {:#x} - {:#x}",
                range.0.get(),
                range.1.get()
            );
        };

        for step in iter {
            match step {
                PtStep::Table { frame, .. } => Frame::init_page_table_frame(frame),
                PtStep::Leaf { .. } | PtStep::Absent { .. } => {}
            }
        }
    }
}

fn create_vmemmap(
    linear_end: FrameNumber,
    block_start: FrameNumber,
    block_end: FrameNumber,
) -> ControlFlow<()> {
    let start = block_start.get() / VMAP_PER_PAGE;
    let end = block_end.get().div_ceil(VMAP_PER_PAGE);
    let count = end - start;
    if block_end > linear_end {
        let page = VMEMMAP_BASE.to_page_number() + start;

        let frame_start = early_allocate_pages(count).to_frame_number();
        let flags = PageFlags::new();

        // 清零
        let page_start = if frame_start + count < linear_end {
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
                linear_end,
                flags,
                false,
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
