use core::{ffi::CStr, num::NonZeroU16, ptr::NonNull};

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        MemoryError, PageCacheType,
        dma::constraints::Constraints,
        frame::{options::FrameAllocOptions, zone::ZoneType},
        kmalloc::kfree,
        page::{PageTableOps, current_root_pt, options::PageAllocOptions},
        slub::{config::CacheConfig, mem_cache::MemCache},
    },
};

#[repr(transparent)]
pub struct Pool {
    cache: MemCache,
}

impl Pool {
    pub fn new(
        name: &'static CStr,
        object_size: NonZeroU16,
        align: usize,
        constraints: &Constraints,
        coherency: bool,
    ) -> Option<NonNull<Self>> {
        let config = CacheConfig::new(name, object_size).ok()?;
        let config = if align > 0 {
            config.align(align)
        } else {
            config
        };

        let mask = PhysAddr::new(constraints.coherent_mask);
        let frame_opts = if mask > ZoneType::MEM32.range().end {
            FrameAllocOptions::new().fallback(&[ZoneType::LinearMem, ZoneType::MEM32])
        } else {
            FrameAllocOptions::new().fallback(&[ZoneType::MEM32])
        };

        let cache_type = if coherency {
            PageCacheType::WriteBack
        } else {
            PageCacheType::Uncached
        };

        let page_opts = PageAllocOptions::new(frame_opts)
            .cache_type(cache_type)
            .contiguous(true)
            .zeroed(true);

        let cache = MemCache::new(config, page_opts)?;

        Some(cache.cast())
    }

    pub fn allocate(&self) -> Result<(VirtAddr, PhysAddr), MemoryError> {
        let ptr = self
            .cache
            .allocate::<u8>()
            .ok_or(MemoryError::OutOfMemory)?;

        let vaddr = VirtAddr::new(ptr.addr().get());
        let paddr = PageTableOps::<ArchPageTable>::translate(current_root_pt(), vaddr)
            .expect("Pool allocation successed but not exist in page table");
        Ok((vaddr, paddr))
    }

    pub const fn object_size(&self) -> usize {
        self.cache.config.object_size.0.get() as usize
    }

    pub fn deallocate<T>(&self, ptr: NonNull<T>) -> Result<(), MemoryError> {
        kfree(ptr)
    }

    pub fn destroy(pool: NonNull<Self>) -> Result<(), MemoryError> {
        if MemCache::try_destory(pool.cast()).is_none() {
            return Err(MemoryError::OutOfMemory);
        }
        kfree(pool.cast::<()>())
    }
}
