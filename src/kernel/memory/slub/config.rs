//! SLUB 缓存配置
//!
//! 提供一组默认的 kmalloc 缓存配置（kmalloc-8 .. kmalloc-4k），
//! 每项包含 name、object_size。

use core::{
    cell::SyncUnsafeCell,
    num::NonZeroU16,
    ptr::{NonNull, null_mut},
    sync::atomic::{AtomicPtr, Ordering},
};

use crate::kernel::memory::slub::mem_cache::MemCache;

#[derive(Debug, Clone, Copy)]
pub struct CacheConfig {
    /// 缓存名称，例如 "kmalloc-8"
    pub name: &'static str,
    /// 对象大小（字节）
    pub object_size: NonZeroU16,
}

/// 默认缓存列表（从 8 到 4096 bytes）
pub const DEFAULT_CACHE_CONFIGS: &[CacheConfig] = &[
    CacheConfig {
        name: "kmalloc-8",
        object_size: NonZeroU16::new(8).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-16",
        object_size: NonZeroU16::new(16).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-32",
        object_size: NonZeroU16::new(32).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-64",
        object_size: NonZeroU16::new(64).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-128",
        object_size: NonZeroU16::new(128).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-256",
        object_size: NonZeroU16::new(256).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-512",
        object_size: NonZeroU16::new(512).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-1k",
        object_size: NonZeroU16::new(1024).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-2k",
        object_size: NonZeroU16::new(2048).unwrap(),
    },
    CacheConfig {
        name: "kmalloc-4k",
        object_size: NonZeroU16::new(4096).unwrap(),
    },
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
