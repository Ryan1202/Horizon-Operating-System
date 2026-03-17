use crate::{
    arch::PhysAddr,
    kernel::memory::frame::{
        FRAME_MANAGER, FrameAllocator, FrameError, FrameNumber, ZoneType, buddy::FrameOrder,
        reference::UniqueFrames, zone::ZONE_COUNT,
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
                chain: [Some(ZoneType::LinearMem), Some(ZoneType::MEM32)],
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

    pub const fn fixed(mut self, start: FrameNumber, order: FrameOrder) -> Self {
        self.alloc_type = FrameAllocType::Fixed { start, order };
        self
    }

    pub const fn get_type(&self) -> FrameAllocType {
        self.alloc_type
    }

    pub const fn get_order(&self) -> FrameOrder {
        match self.alloc_type {
            FrameAllocType::Dynamic { order } => order,
            FrameAllocType::Fixed { order, .. } => order,
        }
    }

    pub fn try_alloc(&self) -> Result<(UniqueFrames, ZoneType), FrameError> {
        for &zone_type in self.fallback.chain.iter().flatten() {
            if let Ok(frame) = self.alloc_type.allocate(zone_type) {
                return Ok(frame);
            }
        }
        Err(FrameError::OutOfFrames)
    }

    pub fn allocate(&self) -> Result<(UniqueFrames, ZoneType), FrameError> {
        self.try_alloc().or_else(|e| match self.retry {
            RetryPolicy::FastFail => Err(e),
            RetryPolicy::Retry(n) => {
                for _ in 0..n {
                    if let Ok(frame) = self.try_alloc() {
                        return Ok(frame);
                    }
                }
                Err(FrameError::OutOfFrames)
            }
        })
    }

    /// 类似 GFP_KERNEL：通用内核分配
    ///
    /// - 优先 LinearMem，fallback 到 MEM32
    /// - 允许重试
    /// - 适用于大多数内核路径
    pub const fn kernel(order: FrameOrder) -> Self {
        Self::new().dynamic(order).retry(RetryPolicy::Retry(3))
    }

    /// 类似 GFP_ATOMIC：原子上下文分配
    ///
    /// - 优先 LinearMem，fallback 到 MEM32
    /// - 不允许重试（FailFast）
    /// - 适用于中断处理、持锁上下文等不能睡眠的场景
    pub const fn atomic(order: FrameOrder) -> Self {
        Self::new().dynamic(order).retry(RetryPolicy::FastFail)
    }

    /// 线性映射区优先（用户页场景）
    ///
    /// - 优先 LinearMem，fallback 到 MEM32
    /// - 适用于用户空间页（不需要内核线性映射）
    pub const fn linear_preferred() -> Self {
        const LINEAR_FALLBACK: [ZoneType; 2] = [ZoneType::LinearMem, ZoneType::MEM32];
        Self::new()
            .fallback(&LINEAR_FALLBACK)
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
        order: FrameOrder,
    },
}

impl FrameAllocType {
    fn allocate(&self, zone: ZoneType) -> Result<(UniqueFrames, ZoneType), FrameError> {
        match self {
            Self::Dynamic { order } => FRAME_MANAGER
                .allocate(zone, *order)
                .map(|f| (f, zone))
                .ok_or(FrameError::OutOfFrames),

            Self::Fixed { start, order } => FRAME_MANAGER.assign(*start, *order).map(|frames| {
                let paddr = PhysAddr::from_frame_number(*start);
                let zone_type = ZoneType::from_address(paddr);

                (frames, zone_type)
            }),
        }
    }
}
