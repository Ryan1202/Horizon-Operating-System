use core::sync::atomic::Ordering;

use crate::{
    arch::{ArchPageTable, PhysAddr},
    kernel::memory::{arch::ArchMemory, frame::zone::RESERVED_END},
};

use super::{
    ALLOCATED_PAGES, FRAME_MANAGER, Frame, FrameData, FrameNumber, FrameRange, FrameTag,
    PAGE_INFO_SIZE, PREALLOCATED_END_PHY, TOTAL_PAGES, frame_count,
};

#[repr(C)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
}

#[unsafe(no_mangle)]
pub extern "C" fn page_early_init(kernel_end: usize) {
    unsafe {
        // 都向后对齐到页
        *PREALLOCATED_END_PHY.get() = PhysAddr::new(kernel_end)
            .max(*PREALLOCATED_END_PHY.get())
            .page_align_up();
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
#[unsafe(no_mangle)]
pub extern "C" fn early_allocate_pages(count: u8) -> usize {
    unsafe {
        let addr = *PREALLOCATED_END_PHY.get();
        *PREALLOCATED_END_PHY.get() += (count as usize) * ArchPageTable::PAGE_SIZE;

        addr.as_usize()
    }
}

/// 启动阶段的初始化函数，仅可在单线程环境使用
impl Frame {
    /// 填充 [start, end) 范围的 `Frame`
    ///
    /// 实际上只填写 start 一个 `Frame`
    fn fill_range(start: FrameNumber, end: FrameNumber, e820_type: u32) {
        debug_assert!(start < end);

        let frame = unsafe { Self::get_raw(start).as_mut() };
        let mut range = FrameRange { start, end };
        let mut data = FrameData { range };

        let tag = match e820_type {
            0 => FrameTag::SystemReserved,
            1 => FrameTag::Free,
            2 | 3 | 4 => FrameTag::HardwareReserved,
            _ => FrameTag::BadMemory,
        };

        if range.end < RESERVED_END {
            // 跳过保留区的可用内存
            return;
        } else if range.start < RESERVED_END {
            // 跨越边界则调整起始地址
            range.start = RESERVED_END;
            data = FrameData { range };
        }

        let count = end.count_from(start);
        match tag {
            FrameTag::Free => {
                TOTAL_PAGES.fetch_add(count, Ordering::Relaxed);
            }
            FrameTag::SystemReserved => {
                TOTAL_PAGES.fetch_add(count, Ordering::Relaxed);
                ALLOCATED_PAGES.fetch_add(count, Ordering::Relaxed);
            }
            _ => {}
        }

        // 写入首个 page 描述
        unsafe { frame.replace(tag, data) };
    }

    /// 初始化页结构体数组，根据E820内存块信息划分内存，返回页结构体数组所需的总字节数
    fn init(
        blocks: *const E820Ards,
        block_count: u16,
        kernel_range: (PhysAddr, PhysAddr),
    ) -> usize {
        let (kernel_start, kernel_end) = (
            kernel_range.0.to_frame_number(),
            kernel_range.1.to_frame_number(),
        );

        let mut last = FrameNumber::new(0);
        // 将每个可用内存块按Buddy的方式分割成块
        for i in 0..block_count {
            let block = unsafe { &*blocks.add(i as usize) };

            // 起始地址向后对齐，避免向前越界
            let start_addr = PhysAddr::new(block.base_addr as usize).page_align_up();
            let block_start = start_addr.to_frame_number();

            let length = block.length as usize - start_addr.page_offset();

            let block_end = block_start + frame_count(length);

            if last < block_start {
                // 填充上一个块和当前块之间的空洞为保留
                Self::fill_range(last, block_start, 2);
            }
            last = block_end;

            if block_start >= block_end {
                continue;
            }

            // 填充 [block_start, block_end) 范围
            if block_end <= kernel_start || block_start >= kernel_end {
                Self::fill_range(block_start, block_end, block.block_type);
            } else {
                // 内存块和内核有重叠部分
                let e820_type = block.block_type;

                // 前半部分可用
                if block_start < kernel_start {
                    Self::fill_range(block_start, kernel_start, e820_type);
                }

                Self::fill_range(kernel_start, kernel_end, 0);

                // 后半部分可用
                if block_end > kernel_end {
                    Self::fill_range(kernel_end, block_end, e820_type);
                }
            }
        }

        PAGE_INFO_SIZE
    }
}
