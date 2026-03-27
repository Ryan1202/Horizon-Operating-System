//! SLUB 缓存配置
//!
//! 提供一组默认的 kmalloc 缓存配置（kmalloc-8 .. kmalloc-4k），
//! 每项包含 name、object_size。

use core::{
    cell::SyncUnsafeCell,
    ffi::CStr,
    num::{NonZeroU16, NonZeroUsize},
    ptr::{NonNull, null_mut},
    sync::atomic::{AtomicPtr, Ordering},
};

use crate::kernel::memory::{
    frame::buddy::FrameOrder,
    slub::{
        MAX_PARTIAL, MIN_ALIGN, MIN_PARTIAL, ObjectSize, SlubError, calculate_sizes,
        mem_cache::MemCache,
    },
};

#[derive(Debug, Clone, Copy)]
pub struct CacheConfig {
    /// 缓存名称，例如 "kmalloc-8"
    pub name: &'static CStr,
    /// 对象原始大小
    pub origin_size: NonZeroU16,
    /// 对齐要求
    pub align: usize,

    // 以下字段由 calculate_sizes 计算得出
    /// 对象大小
    pub object_size: ObjectSize,
    /// 对象数量
    pub object_num: NonZeroU16,
    /// 分配帧的阶数
    pub frame_order: FrameOrder,
    /// partial list 中的最小对象数量（超过该数量时才考虑销毁 slub）
    pub min_partial: u8,
    #[cfg(feature = "slub_debug")]
    /// 用户对象偏移量
    pub user_offset: usize,
}

impl CacheConfig {
    pub const fn new(name: &'static CStr, origin_size: NonZeroU16) -> Result<Self, SlubError> {
        match calculate_sizes(origin_size, MIN_ALIGN) {
            Ok(sizes) => {
                let min_partial = (sizes.0.0.ilog2() as u8 / 2).clamp(MIN_PARTIAL, MAX_PARTIAL);

                let user_offset = {
                    #[cfg(not(feature = "slub_debug"))]
                    {
                        0
                    }

                    #[cfg(feature = "slub_debug")]
                    {
                        super::debug::user_ptr_offset(sizes.3)
                    }
                };

                Ok(Self {
                    name,
                    origin_size,
                    align: sizes.3,
                    object_size: sizes.0,
                    object_num: sizes.1,
                    frame_order: sizes.2,
                    min_partial,
                    user_offset,
                })
            }
            Err(e) => Err(e),
        }
    }

    pub const fn align(mut self, align: usize) -> Self {
        let sizes = calculate_sizes(self.object_size.0, align.max(MIN_ALIGN))
            .ok()
            .unwrap();
        self.object_size = sizes.0;
        self.object_num = sizes.1;
        self.frame_order = sizes.2;
        self.align = sizes.3;
        #[cfg(feature = "slub_debug")]
        {
            self.user_offset = super::debug::user_ptr_offset(sizes.3);
        }
        self
    }
}

/// 默认缓存列表（从 8 到 4096 bytes）
pub const DEFAULT_CACHE_CONFIGS: &[CacheConfig] = &[
    CacheConfig::new(c"kmalloc-8", NonZeroU16::new(8).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-16", NonZeroU16::new(16).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-32", NonZeroU16::new(32).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-64", NonZeroU16::new(64).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-96", NonZeroU16::new(96).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-128", NonZeroU16::new(128).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-192", NonZeroU16::new(192).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-256", NonZeroU16::new(256).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-512", NonZeroU16::new(512).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-1k", NonZeroU16::new(1024).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-2k", NonZeroU16::new(2048).unwrap())
        .ok()
        .unwrap(),
    CacheConfig::new(c"kmalloc-4k", NonZeroU16::new(4096).unwrap())
        .ok()
        .unwrap(),
];

pub const DEFAULT_CACHE_COUNT: usize = DEFAULT_CACHE_CONFIGS.len();

pub static DEFAULT_CACHES: SyncUnsafeCell<[AtomicPtr<MemCache>; DEFAULT_CACHE_COUNT]> =
    SyncUnsafeCell::new([const { AtomicPtr::new(null_mut()) }; DEFAULT_CACHE_COUNT]);

pub fn get_cache(index: usize) -> Option<NonNull<MemCache>> {
    if index >= DEFAULT_CACHE_COUNT {
        return None;
    }

    let cache_ptr = unsafe { (*DEFAULT_CACHES.get())[index].load(Ordering::Acquire) };
    NonNull::new(cache_ptr)
}

pub unsafe fn get_cache_unchecked(index: usize) -> NonNull<MemCache> {
    unsafe {
        let cache_ptr = (*DEFAULT_CACHES.get())[index].load(Ordering::Acquire);
        NonNull::new_unchecked(cache_ptr)
    }
}

pub fn select_cache<'a>(size: NonZeroUsize) -> Option<&'a mut MemCache> {
    let size = size.get().max(8);
    let ilog = size.next_power_of_two().ilog2() as usize;

    match ilog {
        0..=6 => unsafe { Some(get_cache_unchecked(ilog - 3).as_mut()) },
        7 => unsafe { Some(get_cache_unchecked(if size <= 96 { 4 } else { 5 }).as_mut()) },
        8 => unsafe { Some(get_cache_unchecked(if size <= 196 { 6 } else { 7 }).as_mut()) },
        8..=12 => unsafe { Some(get_cache_unchecked(ilog - 1).as_mut()) },
        _ => None,
    }
}
