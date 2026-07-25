use core::{mem::ManuallyDrop, ptr::NonNull};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        MemoryError, PageCacheType,
        arch::ArchMemory,
        dma::{constraints::Constraints, device::Device},
        frame::{buddy::FrameOrder, options::FrameAllocOptions, zone::ZoneType},
        kmalloc::kfree,
        page::options::PageAllocOptions,
    },
};

pub struct CoherentAllocator {
    options: PageAllocOptions,
}

impl CoherentAllocator {
    pub const fn new(constraints: &Constraints, coherency: bool) -> Self {
        let mask = PhysAddr::new(constraints.coherent_mask);
        let frame_options = if mask > ZoneType::MEM32.range().end {
            FrameAllocOptions::new().fallback(&[ZoneType::LinearMem, ZoneType::MEM32])
        } else {
            FrameAllocOptions::new().fallback(&[ZoneType::MEM32])
        };

        let cache_type = if coherency {
            PageCacheType::WriteBack
        } else {
            PageCacheType::Uncached
        };

        CoherentAllocator {
            options: PageAllocOptions::new(frame_options)
                .cache_type(cache_type)
                .contiguous(true)
                .zeroed(true),
        }
    }

    pub fn allocate(
        &self,
        constraints: &Constraints,
        order: FrameOrder,
    ) -> Result<(VirtAddr, PhysAddr), MemoryError> {
        let page = ManuallyDrop::new(self.options.order(order).allocate()?);

        let vaddr = page.start_addr();
        let paddr = page
            .start_paddr()
            .expect("Allocation successed but first frame not exists");

        let alloc_size = order.to_count().get() * ArchPageTable::PAGE_SIZE;
        if constraints.crosses_boundary(paddr, alloc_size) {
            if let Err(e) = kfree(page.get_ptr::<()>()) {
                printk!(
                    "WARN: Failed to free coherent page at {:p}: {:?}",
                    page.start_addr(),
                    e
                );
            }
            return Err(MemoryError::InvalidVirtualAddress(vaddr));
        }

        if !constraints.is_coherent_satisfied(paddr) {
            if let Err(e) = kfree(page.get_ptr::<()>()) {
                printk!(
                    "WARN: Failed to free coherent page at {:p}: {:?}",
                    page.start_addr(),
                    e
                );
            }

            return Err(MemoryError::InvalidVirtualAddress(page.start_addr()));
        }

        Ok((vaddr, paddr))
    }

    pub fn deallocate<T>(&self, ptr: NonNull<T>) -> Result<(), MemoryError> {
        kfree(ptr)
    }
}

pub fn allocate(device: &mut Device, size: usize) -> Result<(VirtAddr, PhysAddr), MemoryError> {
    let order = FrameOrder::from_size(size);

    device
        .coherent_allocator
        .get_or_insert_with(|| CoherentAllocator::new(&device.constraints, device.coherency))
        .allocate(&device.constraints, order)
}

pub fn deallocate(device: &Device, vaddr: VirtAddr) -> Result<(), MemoryError> {
    if let Some(allocator) = device.coherent_allocator.as_ref() {
        allocator.deallocate(NonNull::new(vaddr.as_mut_ptr::<u8>()).unwrap())
    } else {
        printk!(
            "WARNING: Attempt to deallocate coherent memory at {:p} without an allocator\n",
            vaddr
        );
        Err(MemoryError::OtherError)
    }
}
