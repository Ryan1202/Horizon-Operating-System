use core::num::NonZeroUsize;

use crate::{
    arch::{ArchFlushTlb, ArchPageTable},
    kernel::memory::{
        MemoryError, PageCacheType,
        arch::ArchMemory,
        frame::{
            FrameNumber, FrameTag,
            buddy::FrameOrder,
            options::{FrameAllocOptions, FrameAllocType, RetryPolicy},
            zone::ZoneType,
        },
        page::{FlushTlb, Pages, dyn_pages::DynPages, vmap::get_vmap_node},
    },
};

#[derive(Debug, Clone, Copy)]
pub struct PageAllocOptions {
    pub frame: FrameAllocOptions,
    contiguous: bool,
    cache_type: PageCacheType,
    zeroed: bool,
    retry: RetryPolicy,
}

impl PageAllocOptions {
    pub const fn new(frame_options: FrameAllocOptions) -> Self {
        PageAllocOptions {
            frame: frame_options,
            contiguous: true,
            cache_type: PageCacheType::WriteBack,
            zeroed: false,
            retry: RetryPolicy::FastFail,
        }
    }

    pub const fn contiguous(mut self, contiguous: bool) -> Self {
        self.contiguous = contiguous;
        self
    }

    pub const fn cache_type(mut self, cache: PageCacheType) -> Self {
        self.cache_type = cache;
        self
    }

    pub const fn zeroed(mut self, zeroed: bool) -> Self {
        self.zeroed = zeroed;
        self
    }

    pub const fn retry(mut self, retry: RetryPolicy) -> Self {
        self.retry = retry;
        self
    }

    pub fn get_frame_options(&self) -> &FrameAllocOptions {
        &self.frame
    }

    /// 预设选项：适用于内核常规分配
    pub const fn kernel(order: FrameOrder) -> Self {
        Self::new(FrameAllocOptions::atomic(order)).retry(RetryPolicy::Retry(3))
    }

    /// 预设选项：适用于原子上下文分配
    pub const fn atomic(order: FrameOrder) -> Self {
        Self::new(FrameAllocOptions::atomic(order)).retry(RetryPolicy::FastFail)
    }

    /// 预设选项: IO 内存分配
    pub const fn mmio(start: FrameNumber, count: NonZeroUsize, cache: PageCacheType) -> Self {
        Self::new(FrameAllocOptions::new().fixed(start, count)).cache_type(cache)
    }
}

impl PageAllocOptions {
    fn alloc_discontiguous(&self, pages: &mut DynPages) -> Result<(), MemoryError> {
        let mut order = if let FrameAllocType::Dynamic { order } = *self.frame.get_type() {
            order
        } else {
            panic!("Discontiguous allocation only support dynamic frame allocation!")
        };

        let mut remaining = order.to_count().get();
        let mut first = None;

        while remaining > 0 {
            let option = self.frame.dynamic(order);
            let result = option.allocate();

            match result {
                Ok((mut _frames, _zone)) => {
                    debug_assert!(matches!(_frames.get_tag(), FrameTag::Allocated));

                    pages.map::<ArchPageTable>(_frames, order.to_count().get(), self.cache_type)?;

                    if first.is_none() {
                        first = Some(());
                    }

                    remaining -= order.to_count().get();
                }
                Err(_) => {
                    // 当前 order 失败，尝试更小的 order
                    if order.get() == 0 {
                        // order 已是最小，无法继续降低
                        break;
                    }
                    order = order - 1;
                }
            }
        }

        // 分配结果判断
        if remaining == 0 {
            // 完全满足需求
            let start = pages.start_addr().to_page_number().unwrap();
            let end = start + pages.frame_count - 1;
            ArchFlushTlb::flush_range(start, end);
            Ok(())
        } else if first.is_some() {
            // 部分成功但未满足需求，返回错误（避免返回残留的链表）
            pages.unlink::<ArchPageTable>()?;
            Err(MemoryError::OutOfMemory)
        } else {
            // 完全失败
            Err(MemoryError::OutOfMemory)
        }
    }

    fn try_alloc<'a>(&self) -> Result<Pages<'a>, MemoryError> {
        let count = self.frame.get_count();

        if self.contiguous {
            let (frame, zone) = self.frame.allocate()?;

            if !matches!(zone, ZoneType::LinearMem) {
                let v = unsafe { get_vmap_node().allocate(count)?.as_mut() };

                v.map::<ArchPageTable>(frame, count.get(), self.cache_type)?;

                let start = v.start_addr().to_page_number().unwrap();
                let end = start + v.frame_count - 1;
                ArchFlushTlb::flush_range(start, end);

                Ok(Pages::Dynamic(v))
            } else {
                Ok(Pages::Fixed((frame, count.get())))
            }
        } else {
            let v = unsafe { get_vmap_node().allocate(count)?.as_mut() };

            let result = self.alloc_discontiguous(v);

            if result.is_err() {
                let _ = get_vmap_node().deallocate(v).inspect_err(|e| {
                    printk!(
                        "Failed to free virtual memory since {}, error: {:?} (memory leaked)",
                        v.start_addr(),
                        e
                    )
                });

                result?;
            }

            Ok(Pages::Dynamic(v))
        }
    }

    pub fn allocate<'a>(&self) -> Result<Pages<'a>, MemoryError> {
        let mut pages = self.try_alloc().or_else(|e| match self.retry {
            RetryPolicy::FastFail => Err(e),
            RetryPolicy::Retry(n) => {
                for _ in 0..n {
                    if let Ok(addr) = self.try_alloc() {
                        return Ok(addr);
                    }
                }
                Err(e)
            }
        });

        if let Ok(ref mut pages) = pages
            && self.zeroed
        {
            unsafe {
                pages
                    .get_ptr::<u8>()
                    .write_bytes(0, self.frame.get_count().get() * ArchPageTable::PAGE_SIZE)
            };
        }

        pages
    }
}
