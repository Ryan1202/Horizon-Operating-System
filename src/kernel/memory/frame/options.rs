use core::num::NonZeroUsize;

use crate::{
    arch::PhysAddr,
    kernel::memory::frame::{
        FRAME_MANAGER, Frame, FrameAllocator, FrameError, FrameNumber, ZoneType, buddy::FrameOrder,
        reference::FrameMut, zone::ZONE_COUNT,
    },
};

#[derive(Debug, Clone, Copy)]
pub struct FrameAllocOptions {
    alloc_type: FrameAllocType,
    fallback: FallbackChain,
    retry: RetryPolicy,
}

impl FrameAllocOptions {
    pub const fn new() -> Self {
        Self {
            alloc_type: FrameAllocType::Dynamic {
                order: FrameOrder::new(0),
            },
            fallback: FallbackChain {
                chain: [
                    Some(ZoneType::LinearMem),
                    Some(ZoneType::HighMem),
                    Some(ZoneType::MEM24),
                ],
            },
            retry: RetryPolicy::FastFail,
        }
    }

    pub const fn fallback(mut self, zone_types: &[ZoneType]) -> Self {
        let mut i = 0;
        while i < zone_types.len() && i < ZONE_COUNT {
            self.fallback.chain[i] = Some(zone_types[i]);
            i += 1;
        }
        self
    }

    pub const fn retry(mut self, retry: RetryPolicy) -> Self {
        self.retry = retry;
        self
    }

    pub const fn dynamic(mut self, order: FrameOrder) -> Self {
        self.alloc_type = FrameAllocType::Dynamic { order };
        self
    }

    pub const fn fixed(mut self, start: FrameNumber, count: NonZeroUsize) -> Self {
        self.alloc_type = FrameAllocType::Fixed { start, count };
        self
    }

    pub fn get_type(&self) -> &FrameAllocType {
        &self.alloc_type
    }

    pub fn get_count(&self) -> NonZeroUsize {
        match &self.alloc_type {
            FrameAllocType::Dynamic { order } => order.to_count(),
            FrameAllocType::Fixed { count, .. } => *count,
        }
    }

    pub fn try_alloc(&self) -> Result<(FrameMut, ZoneType), FrameError> {
        for &zone_type in self.fallback.chain.iter().flatten() {
            if let Ok(frame) = self.alloc_type.allocate(zone_type) {
                return Ok(frame);
            }
        }
        Err(FrameError::OutOfFrames)
    }

    pub fn allocate(&self) -> Result<(FrameMut, ZoneType), FrameError> {
        self.try_alloc().or_else(|e| match self.retry {
            RetryPolicy::FastFail => Err(e),
            RetryPolicy::Retry(n) => {
                for _ in 0..n {
                    if let Ok(frame) = self.allocate() {
                        return Ok(frame);
                    }
                }
                Err(FrameError::OutOfFrames)
            }
        })
    }

    /// 类似 GFP_KERNEL：通用内核分配
    ///
    /// - 优先 LinearMem，fallback 到 MEM24
    /// - 允许重试
    /// - 适用于大多数内核路径
    pub const fn kernel(order: FrameOrder) -> Self {
        Self::new().dynamic(order).retry(RetryPolicy::Retry(3))
    }

    /// 类似 GFP_ATOMIC：原子上下文分配
    ///
    /// - 优先 LinearMem，fallback 到 MEM24
    /// - 不允许重试（FailFast）
    /// - 适用于中断处理、持锁上下文等不能睡眠的场景
    pub const fn atomic(order: FrameOrder) -> Self {
        Self::new().dynamic(order).retry(RetryPolicy::FastFail)
    }

    /// 类似 GFP_HIGHUSER：高端内存优先
    ///
    /// - 优先 HighMem，fallback 到 LinearMem
    /// - 适用于用户空间页（不需要内核线性映射）
    pub const fn highmem() -> Self {
        const HIGHMEM_FALLBACK: [ZoneType; 3] =
            [ZoneType::HighMem, ZoneType::LinearMem, ZoneType::MEM24];
        Self::new()
            .fallback(&HIGHMEM_FALLBACK)
            .retry(RetryPolicy::Retry(3))
    }
}

impl Default for FrameAllocOptions {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Debug, Clone, Copy)]
pub struct FallbackChain {
    chain: [Option<ZoneType>; ZONE_COUNT],
}

#[derive(Debug, Clone, Copy)]
pub enum RetryPolicy {
    FastFail,
    Retry(usize),
}

#[derive(Debug, Clone, Copy)]
pub enum FrameAllocType {
    Dynamic {
        order: FrameOrder,
    },
    Fixed {
        start: FrameNumber,
        count: NonZeroUsize,
    },
}

impl FrameAllocType {
    fn allocate(&self, zone: ZoneType) -> Result<(FrameMut, ZoneType), FrameError> {
        match self {
            Self::Dynamic { order } => FRAME_MANAGER
                .allocate(zone, *order)
                .map(|f| (f, zone))
                .ok_or(FrameError::OutOfFrames),

            Self::Fixed { start, count } => {
                FRAME_MANAGER.assign(*start, count.get()).and_then(|_| {
                    let frame = Frame::get_mut(*start).ok_or(FrameError::Conflict)?;

                    let zone_type = ZoneType::from_address(PhysAddr::from_frame_number(*start));
                    Ok((frame, zone_type))
                })
            }
        }
    }
}
