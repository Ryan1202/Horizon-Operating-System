//! SLUB 缓存配置
//!
//! 提供一组默认的 kmalloc 缓存配置（kmalloc-8 .. kmalloc-4k），
//! 每项包含 name、object_size。

pub(super) const MIN_OBJECTS_PER_SLAB: usize = 1;

#[derive(Debug, Clone, Copy)]
pub struct CacheConfig {
    /// 缓存名称，例如 "kmalloc-8"
    pub name: &'static str,
    /// 对象大小（字节）
    pub object_size: usize,
}

/// 默认缓存列表（从 8 到 4096 bytes）
pub static DEFAULT_CACHES: &'static [CacheConfig] = &[
    CacheConfig {
        name: "kmalloc-8",
        object_size: 8,
    },
    CacheConfig {
        name: "kmalloc-16",
        object_size: 16,
    },
    CacheConfig {
        name: "kmalloc-32",
        object_size: 32,
    },
    CacheConfig {
        name: "kmalloc-64",
        object_size: 64,
    },
    CacheConfig {
        name: "kmalloc-128",
        object_size: 128,
    },
    CacheConfig {
        name: "kmalloc-256",
        object_size: 256,
    },
    CacheConfig {
        name: "kmalloc-512",
        object_size: 512,
    },
    CacheConfig {
        name: "kmalloc-1k",
        object_size: 1024,
    },
    CacheConfig {
        name: "kmalloc-2k",
        object_size: 2048,
    },
    CacheConfig {
        name: "kmalloc-4k",
        object_size: 4096,
    },
];

impl CacheConfig {
    /// 计算每个 slab 中可容纳的对象数量（假定页大小为 4096 字节）
    pub fn objects_per_slab(&self, page_size: usize) -> usize {
        todo!()
    }
}
