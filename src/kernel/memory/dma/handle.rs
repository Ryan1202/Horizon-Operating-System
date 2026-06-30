use core::ptr::NonNull;

use crate::{
    arch::{PhysAddr, VirtAddr},
    kernel::memory::{
        MemoryError,
        dma::{
            Direction, DmaAddr,
            device::Device,
            source::{BouncePool, Source, software_iotlb::BounceAddr},
            sync::Sync,
        },
    },
};

/// DMA 句柄，表示一个 DMA 缓冲区的映射关系
///
/// drop 时会自动释放资源
pub struct DmaHandle {
    device: NonNull<Device>,
    source: Source,
    vaddr: VirtAddr,
    paddr: PhysAddr,
    dma_addr: DmaAddr,
    size: usize,
    direction: Direction,
}

impl DmaHandle {
    pub fn new(
        device: &mut Device,
        source: Source,
        vaddr: Option<VirtAddr>,
        size: usize,
        direction: Direction,
    ) -> Result<Self, MemoryError> {
        let (acquired_vaddr, paddr) = source.acquire(device)?;
        let vaddr = acquired_vaddr
            .or(vaddr)
            .ok_or(MemoryError::InvalidVirtualAddress(VirtAddr::new(0)))?;

        match device.backend.map(paddr, size) {
            Ok(dma_addr) => Ok(Self {
                device: NonNull::from_ref(device),
                source,
                vaddr,
                paddr,
                dma_addr,
                size,
                direction,
            }),
            Err(e) => {
                printk!(
                    "WARNING: Failed to release source after backend.map() failed: {:?}\n",
                    e
                );
                Err(e)
            }
        }
    }

    /// 获取同步类型以调用 `sync_for_device` 或 `sync_for_cpu`
    ///
    /// `base`: 需要同步的物理基地址
    ///
    /// `offset`: 偏移量
    ///
    /// `size`: 需要同步的大小，如果为 `None`，则使用整个缓冲区的大小
    fn sync_type(&self, base: PhysAddr, offset: usize, size: usize) -> Result<Sync, MemoryError> {
        match self.source {
            Source::Coherent { .. } | Source::Direct { .. } | Source::Pool { .. } => Ok(Sync::None),
            Source::SoftwareIotlb { .. } => {
                let pool = BouncePool::get_pool();
                let addr = BounceAddr::new(base)
                    .ok_or(MemoryError::InvalidPhysicalAddress(base + offset))?;
                let slot = pool.clone_slot(addr);

                let size = (offset + size).min(slot.origin_size()) - offset;

                Ok(Sync::Bounce(addr, slot, size))
            }
        }
    }

    pub const fn cpu_addr(&self) -> VirtAddr {
        self.vaddr
    }

    pub const fn phys_addr(&self) -> PhysAddr {
        self.paddr
    }

    pub const fn dma_addr(&self) -> DmaAddr {
        self.dma_addr
    }

    pub const fn size(&self) -> usize {
        self.size
    }

    pub const fn direction(&self) -> Direction {
        self.direction
    }

    fn deallocate(&mut self) -> Result<(), MemoryError> {
        let device = unsafe { self.device.as_ref() };

        if let Err(e) = self.sync_range_for_cpu(0, self.size) {
            printk!(
                "WARNING: Failed to sync DMA buffer for CPU before deallocation: {:?}\n",
                e
            );
        }

        let mut result = device.backend.unmap(self.dma_addr);
        if let Err(e) = self.source.release(device, self.vaddr, self.paddr) {
            if result.is_ok() {
                result = Err(e);
            }
        }
        result
    }
}

impl DmaHandle {
    /// 使更新对 CPU 可见
    ///
    /// 这通常在 DMA 写入数据后调用，以确保 CPU 可以看到最新的数据
    #[inline]
    pub fn prepare_for_cpu(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
        let device = unsafe { self.device.as_ref() };
        device.backend.prepare_for_cpu(self.dma_addr + offset, size)
    }

    /// 使更新对设备可见
    ///
    /// 这通常在 CPU 写入数据后调用，以确保设备可以看到最新的数据
    #[inline]
    pub fn prepare_for_device(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
        let device = unsafe { self.device.as_ref() };
        device
            .backend
            .prepare_for_device(self.dma_addr + offset, size)
    }

    /// 向设备同步数据
    pub fn sync_range_for_device(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
        match self.direction {
            Direction::Bidirectional | Direction::ToDevice => {
                self.sync_type(self.paddr, offset, size)?
                    .sync_for_device()?;
            }
            _ => {}
        }

        self.prepare_for_device(offset, size)?;

        Ok(())
    }

    /// 向 CPU 同步数据
    pub fn sync_range_for_cpu(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
        self.prepare_for_cpu(offset, size)?;

        match self.direction {
            Direction::Bidirectional | Direction::FromDevice => {
                self.sync_type(self.paddr, offset, size)?.sync_for_cpu()
            }
            _ => Ok(()),
        }
    }
}

impl Drop for DmaHandle {
    fn drop(&mut self) {
        if let Err(e) = self.deallocate() {
            printk!("WARNING: Failed to deallocate DMA handle: {:?}\n", e);
        }
    }
}
