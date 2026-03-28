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

/// 唯一数据源：声明所有 kmalloc 缓存的 (名称, 大小) 对。
/// 从这里派生出 `KMALLOC_SIZES`、`KMALLOC_NAMES` 和 `DEFAULT_CACHE_CONFIGS`。
macro_rules! kmalloc_caches {
    ($(($name:expr, $size:expr)),+ $(,)?) => {
        /// 所有 kmalloc 缓存的原始大小（字节），升序排列。
        pub const KMALLOC_SIZES: [u16; 0 $(+ { let _: u16 = $size; 1 })+] = [$($size),+];

        /// 所有 kmalloc 缓存的名称。
        pub const KMALLOC_NAMES: [&CStr; 0 $(+ { let _: &CStr = $name; 1 })+] = [$($name),+];

        /// 默认缓存列表，由 `(name, size)` 对生成。
        pub const DEFAULT_CACHE_CONFIGS: &[CacheConfig] = &[$(
            match CacheConfig::new($name, NonZeroU16::new($size).unwrap()) {
                Ok(c) => c,
                Err(_) => panic!("failed to create cache config"),
            }
        ),+];
    };
}

kmalloc_caches!(
    (c"kmalloc-8", 8),
    (c"kmalloc-16", 16),
    (c"kmalloc-32", 32),
    (c"kmalloc-64", 64),
    (c"kmalloc-96", 96),
    (c"kmalloc-128", 128),
    (c"kmalloc-192", 192),
    (c"kmalloc-256", 256),
    (c"kmalloc-512", 512),
    (c"kmalloc-1k", 1024),
    (c"kmalloc-2k", 2048),
    (c"kmalloc-4k", 4096),
);

/// 给定请求大小，返回应使用的缓存索引。
/// 找到 `KMALLOC_SIZES` 中第一个 >= `size` 的条目；全部超出则返回 `None`。
pub const fn size_to_index(size: usize) -> Option<usize> {
    let mut i = 0;
    while i < KMALLOC_SIZES.len() {
        if size <= KMALLOC_SIZES[i] as usize {
            return Some(i);
        }
        i += 1;
    }
    None
}

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

                Ok(Self {
                    name,
                    origin_size,
                    align: sizes.3,
                    object_size: sizes.0,
                    object_num: sizes.1,
                    frame_order: sizes.2,
                    min_partial,
                    #[cfg(feature = "slub_debug")]
                    user_offset: super::debug::user_ptr_offset(sizes.3),
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

pub const DEFAULT_CACHE_COUNT: usize = KMALLOC_NAMES.len();

pub static DEFAULT_CACHES: SyncUnsafeCell<[AtomicPtr<MemCache>; DEFAULT_CACHE_COUNT]> =
    SyncUnsafeCell::new([const { AtomicPtr::new(null_mut()) }; DEFAULT_CACHE_COUNT]);

pub unsafe fn get_cache_unchecked(index: usize) -> NonNull<MemCache> {
    unsafe {
        let cache_ptr = (*DEFAULT_CACHES.get())[index].load(Ordering::Acquire);
        NonNull::new_unchecked(cache_ptr)
    }
}

pub fn select_cache<'a>(size: NonZeroUsize) -> Option<&'a mut MemCache> {
    let index = size_to_index(size.get())?;
    unsafe { Some(get_cache_unchecked(index).as_mut()) }
}
