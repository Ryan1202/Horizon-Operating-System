use core::num::NonZeroUsize;

use crate::kernel::memory::frame::{
    FRAME_MANAGER, Frame, FrameAllocator, FrameError, FrameNumber, ZoneType, buddy::FrameOrder,
    reference::FrameMut, zone::ZONE_COUNT,
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

    pub fn fallback(mut self, zone_types: &[ZoneType]) -> Self {
        for (i, &zone_type) in zone_types.iter().take(3).enumerate() {
            self.fallback.chain[i] = Some(zone_type);
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
        self.alloc_type = FrameAllocType::Static { start, count };
        self
    }

    pub fn get_type(&self) -> &FrameAllocType {
        &self.alloc_type
    }

    pub fn get_count(&self) -> NonZeroUsize {
        match &self.alloc_type {
            FrameAllocType::Dynamic { order } => order.to_count(),
            FrameAllocType::Static { count, .. } => *count,
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
    Static {
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
            Self::Static { start, count } => {
                FRAME_MANAGER.assign(*start, count.get()).and_then(|_| {
                    Ok((
                        Frame::get_mut(*start).ok_or(FrameError::Conflict)?,
                        ZoneType::from_address(start.get()),
                    ))
                })
            }
        }
    }
}
