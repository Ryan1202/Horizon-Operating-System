use core::ptr::copy_nonoverlapping;

use crate::{
    arch::ArchPageTable,
    kernel::memory::{
        MemoryError,
        arch::ArchMemory,
        dma::{
            Direction,
            source::{BouncePool, SlotMeta, software_iotlb::BounceAddr},
        },
        frame::{buddy::FrameOrder, options::FrameAllocOptions},
        page::options::PageAllocOptions,
        vmalloc::vfree,
    },
};

pub struct BounceSync;

impl BounceSync {
    /// 复制数据
    ///
    /// `origin`: 原始数据的虚拟地址
    ///
    /// `bounce`: Bounce Buffer 的虚拟地址
    ///
    /// `size`: 需要复制的数据大小
    ///
    /// `direction`: 数据传输的方向，仅支持 `ToDevice` 和 `FromDevice，不支持` `Bidirectional`
    const fn copy(origin: *mut u8, bounce: *mut u8, size: usize, direction: Direction) {
        unsafe {
            match direction {
                Direction::ToDevice => copy_nonoverlapping(origin, bounce, size),
                Direction::FromDevice => copy_nonoverlapping(bounce, origin, size),
                _ => unreachable!(),
            }
        }
    }

    /// 同步 Bounce Buffer 与原始缓冲区
    ///
    /// `addr`: Bounce Buffer 的物理地址
    ///
    /// `bounce`: Bounce Buffer 的元数据
    ///
    /// `size`: 需要同步的数据大小
    ///
    /// `direction`: 数据传输的方向，仅支持 `ToDevice` 和 `FromDevice，不支持 `Bidirectional`
    pub fn sync(
        addr: BounceAddr,
        bounce: &SlotMeta,
        size: usize,
        direction: Direction,
    ) -> Result<(), MemoryError> {
        let pool = BouncePool::get_pool();

        let addr_start = addr.align_to_slot();
        let device_addr = pool.get_ptr(addr_start);

        let offset = addr
            .offset_from(addr_start)
            .ok_or(MemoryError::InvalidPhysicalAddress(addr.as_paddr()))?;
        let size = size.min(bounce.origin_size() - offset);

        let origin_virt = bounce
            .origin_addr()
            .try_to_virt()
            .ok_or(MemoryError::UnavailableFrame);

        if let Ok(cpu_addr) = origin_virt {
            let bounce = unsafe { (device_addr.byte_offset(offset as isize)).as_ptr() };

            Self::copy((cpu_addr + offset).as_mut_ptr(), bounce, size, direction);
        } else {
            // 如果物理地址无法直接映射到内核虚拟地址空间，我们需要通过分配一个新的页面来进行 Bounce Buffer 的映射
            let mut copied = 0;
            while copied < size {
                let origin_addr = bounce.origin_addr() + offset + copied;
                let origin_offset = origin_addr.page_offset();
                let chunk = (size - copied).min(ArchPageTable::PAGE_SIZE - origin_offset);

                let frame_options = FrameAllocOptions::new()
                    .fixed(origin_addr.to_frame_number(), FrameOrder::new(0));

                let page = PageAllocOptions::new(frame_options).allocate()?;
                let cpu_addr = page.start_addr();

                let bounce =
                    unsafe { (device_addr.byte_offset((offset + copied) as isize)).as_ptr() };

                Self::copy(
                    (cpu_addr + origin_offset).as_mut_ptr(),
                    bounce,
                    chunk,
                    direction,
                );

                vfree(cpu_addr)?;
                copied += chunk;
            }
        }
        Ok(())
    }
}
