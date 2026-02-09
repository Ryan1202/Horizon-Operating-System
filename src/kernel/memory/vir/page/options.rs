use crate::{
    arch::x86::kernel::page::PAGE_SIZE,
    kernel::memory::{
        MemoryError, PageCacheType,
        phy::frame::{
            FrameError, FrameTag,
            options::{FrameAllocOptions, FrameAllocType, RetryPolicy},
            zone::ZoneType,
        },
        vir::{
            page::{Pages, VirtPages},
            vmap::get_vmap_node,
        },
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
            contiguous: false,
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
}

impl PageAllocOptions {
    fn alloc_discontiguous<'a>(&self, pages: &'a mut VirtPages) -> Result<(), FrameError> {
        let mut order = if let FrameAllocType::Dynamic { order } = *self.frame.get_type() {
            order
        } else {
            panic!("Discontiguous allocation only support dynamic frame allocation!")
        };

        let mut remaining = order.to_count().get();
        let mut first = None;

        while remaining > 0 {
            let option = self.clone().frame.dynamic(order);
            let result = option.allocate();

            match result {
                Ok((mut _frames, _zone)) => {
                    debug_assert!(matches!(_frames.get_tag(), FrameTag::Allocated));

                    pages.link(_frames, order.to_count().get(), self.cache_type);

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
            Ok(())
        } else if first.is_some() {
            // 部分成功但未满足需求，返回错误（避免返回残留的链表）
            pages.unlink();
            Err(FrameError::OutOfFrames)
        } else {
            // 完全失败
            Err(FrameError::OutOfFrames)
        }
    }

    fn try_alloc<'a>(&self) -> Result<Pages<'a>, MemoryError> {
        let count = self.frame.get_count();

        if self.contiguous {
            let (frame, zone) = self.frame.allocate().map_err(MemoryError::FrameError)?;

            if !matches!(zone, ZoneType::LinearMem) {
                let node = get_vmap_node();
                let v = unsafe { node.allocate(count)?.as_mut() };

                v.link(frame, count.get(), self.cache_type);

                Ok(Pages::Dynamic(v))
            } else {
                Ok(Pages::Fixed((frame, count.get())))
            }
        } else {
            let node = get_vmap_node();
            let v = unsafe { node.allocate(count)?.as_mut() };

            self.alloc_discontiguous(v)
                .map_err(MemoryError::FrameError)
                .inspect_err(|_| node.deallocate(v).unwrap())?;

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
                    .write_bytes(0, self.frame.get_count().get() * PAGE_SIZE)
            };
        }

        pages
    }
}
