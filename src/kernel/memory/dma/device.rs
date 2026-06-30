use core::{ffi::CStr, num::NonZeroU16, ptr::NonNull};

use crate::{
    arch::{ArchPageTable, VirtAddr},
    kernel::memory::{
        MemoryError,
        dma::{
            Direction, DmaAddr,
            constraints::Constraints,
            handle::DmaHandle,
            mapping::Backend,
            scatter_gather::EntryList,
            source::{
                BouncePool, Source, coherent::CoherentAllocator, direct, pool::Pool,
                software_iotlb::BounceAddr,
            },
            sync::{sync_sg_for_cpu, sync_sg_for_device},
        },
        frame::Frame,
        page::{PageTableOps, current_root_pt},
    },
};

pub struct Device {
    pub(super) constraints: Constraints,
    pub(super) backend: Backend,
    pub(super) coherency: bool,

    pub(super) coherent_allocator: Option<CoherentAllocator>,
}

impl Device {
    pub const fn new(constraints: Constraints, backend: Backend, coherency: bool) -> Self {
        Device {
            constraints,
            backend,
            coherency,
            coherent_allocator: None,
        }
    }

    /// 根据给定的字节大小分配一块对齐到 FrameOrder 的 DMA 内存，返回 CPU 虚拟地址和 DMA 地址。
    pub fn alloc_coherent(&mut self, size: usize) -> Result<DmaHandle, MemoryError> {
        DmaHandle::new(
            self,
            Source::Coherent { size },
            None,
            size,
            Direction::Bidirectional,
        )
    }

    pub fn create_pool(
        &self,
        name: &'static CStr,
        object_size: NonZeroU16,
        align: usize,
    ) -> Option<NonNull<Pool>> {
        Pool::new(name, object_size, align, &self.constraints, self.coherency)
    }

    pub fn alloc_pool(&mut self, pool: NonNull<Pool>) -> Result<DmaHandle, MemoryError> {
        let size = unsafe { pool.as_ref() }.object_size();
        DmaHandle::new(
            self,
            Source::Pool { pool },
            None,
            size,
            Direction::Bidirectional,
        )
    }

    pub fn map_sg(
        &mut self,
        entries: &mut EntryList,
        n_entries: usize,
    ) -> Result<usize, MemoryError> {
        let mut mapped = 0;
        let mut result = Ok(());

        {
            let iter = entries.iter_mut();

            for entry in iter.take(n_entries) {
                if entry.is_empty() {
                    continue;
                }

                if self.constraints.max_segments > 0
                    && mapped >= self.constraints.max_segments as usize
                {
                    result = Err(MemoryError::ViolateConstraint);
                    break;
                }

                let paddr = match entry.phys_addr() {
                    Some(phys) => phys,
                    None => {
                        result = Err(MemoryError::OtherError);
                        break;
                    }
                };

                let size = entry.size();
                let need_bounce = (paddr.as_usize() & !self.constraints.mask) != 0
                    || self.constraints.crosses_boundary(paddr, size)
                    || self.constraints.exceeds_max_segment_size(size);

                let source = if !need_bounce {
                    Source::Direct { paddr, size }
                } else {
                    Source::SoftwareIotlb { paddr, size }
                };

                let dma_paddr = match source.acquire(self) {
                    Ok((_, paddr)) => paddr,
                    Err(e) => {
                        result = Err(e);
                        break;
                    }
                };

                entry.force_set_frame(
                    Frame::get_raw(dma_paddr.to_frame_number()),
                    dma_paddr.page_offset(),
                    size,
                );

                let dma_addr = match self.backend.map(dma_paddr, size) {
                    Ok(addr) => addr,
                    Err(e) => {
                        let _ = source.release(self, VirtAddr::new(0), dma_paddr);
                        result = Err(e);
                        break;
                    }
                };

                entry.set_dma_addr(dma_addr);
                mapped += 1;
            }
        }

        match result {
            Ok(_) => Ok(mapped),
            Err(e) => {
                // 出错时，回滚已映射的段，此时不需要同步任何数据
                self.unmap_sg(entries, mapped)?;
                Err(e)
            }
        }
    }

    pub fn unmap_sg(&self, entries: &mut EntryList, n_entries: usize) -> Result<(), MemoryError> {
        let mut result = Ok(());

        let iter = entries.iter_mut();

        for entry in iter.take(n_entries) {
            if entry.is_empty() {
                continue;
            }

            let dma_addr = entry.dma_addr();
            if dma_addr.as_usize() == 0 {
                continue;
            }

            let paddr = entry.phys_addr();

            if let Err(e) = self.backend.unmap(dma_addr) {
                printk!(
                    "WARNING: Failed to unmap DMA address {:#x}: {:?}\n",
                    dma_addr.as_usize(),
                    e
                );
                if result.is_ok() {
                    result = Err(e);
                }
                continue;
            }

            entry.set_dma_addr(DmaAddr::new(0));

            if let Some(paddr) = paddr {
                let pool = BouncePool::get_pool();
                if let Some(addr) = BounceAddr::new(paddr) {
                    pool.deallocate(addr);
                }
            }
        }

        result
    }

    pub fn sync_sg_for_device(
        &self,
        entries: &mut EntryList,
        n_entries: usize,
        direction: Direction,
    ) -> Result<(), MemoryError> {
        match direction {
            Direction::Bidirectional | Direction::ToDevice => {
                sync_sg_for_device(entries, n_entries)?;
            }
            _ => {}
        }

        for entry in entries.iter().take(n_entries) {
            let dma_addr = entry.dma_addr();
            if dma_addr.as_usize() != 0 {
                self.backend.prepare_for_device(dma_addr, entry.size())?;
            }
        }

        Ok(())
    }

    pub fn sync_sg_for_cpu(
        &self,
        entries: &mut EntryList,
        n_entries: usize,
        direction: Direction,
    ) -> Result<(), MemoryError> {
        for entry in entries.iter().take(n_entries) {
            let dma_addr = entry.dma_addr();
            if dma_addr.as_usize() != 0 {
                self.backend.prepare_for_cpu(dma_addr, entry.size())?;
            }
        }

        match direction {
            Direction::Bidirectional | Direction::FromDevice => {
                sync_sg_for_cpu(entries, n_entries)?;
            }
            _ => {}
        }

        Ok(())
    }

    /// 流式 DMA 单段映射。
    ///
    /// 物理连续且在 DMA mask 内 → 直接映射；否则分配 bounce buffer。
    pub fn map_single<T>(
        &mut self,
        ptr: NonNull<T>,
        size: usize,
        direction: Direction,
    ) -> Result<DmaHandle, MemoryError> {
        let vaddr = VirtAddr::new(ptr.addr().get());

        let paddr = direct::translate(vaddr, size);

        let source = if let Some(paddr) = paddr
            && (paddr.as_usize() & !self.constraints.mask == 0
                && !self.constraints.crosses_boundary(paddr, size)
                && !self.constraints.exceeds_max_segment_size(size))
        {
            Source::Direct { paddr, size }
        } else {
            let pt = current_root_pt();
            let paddr = PageTableOps::<ArchPageTable>::translate(pt, vaddr)
                .ok_or(MemoryError::UnavailableFrame)?;
            Source::SoftwareIotlb { paddr, size }
        };

        let handle = DmaHandle::new(self, source, Some(vaddr), size, direction)?;
        if matches!(direction, Direction::ToDevice | Direction::Bidirectional) {
            if let Err(e) = handle.sync_range_for_device(0, size) {
                printk!(
                    "WARNING: Failed to sync single DMA mapping for device after mapping: {:?}\n",
                    e
                );
            }
        }
        Ok(handle)
    }

    /// 流式 DMA 单段解映射。
    pub fn unmap_single(&self, dma_handle: DmaHandle) -> Result<(), MemoryError> {
        let result = if matches!(
            dma_handle.direction(),
            Direction::FromDevice | Direction::Bidirectional
        ) {
            dma_handle.sync_range_for_cpu(0, dma_handle.size())
        } else {
            Ok(())
        };

        if let Err(e) = &result {
            printk!(
                "WARNING: Failed to sync single DMA mapping for CPU before unmapping: {:?}\n",
                e
            );
        }
        result
    }
}
