use core::alloc::Layout;

use crate::{
    kernel::memory::{MemoryError, page::Pages, percpu::area::ALLOC_UNIT},
    lib::rust::{bitmap::Bitmap, spinlock::Spinlock},
};

pub const DYNAMIC_TARGET_SIZE: usize = 28 * 1024;

pub struct PerCpuChunk {
    pages: Pages,
    bitmap: Spinlock<Bitmap<{ ALLOC_UNIT }>>,

    /// 动态分配区相对每个 CPU unit 起点的位置，单位为字节
    dynamic_start: usize,
}

impl PerCpuChunk {
    pub fn try_new(
        pages: Pages,
        dynamic_start: usize,
        unit_size: usize,
    ) -> Result<Self, MemoryError> {
        let dynamic_capacity = unit_size - dynamic_start;
        let bitmap = Spinlock::new(Bitmap::try_new(dynamic_capacity as u32)?);

        Ok(Self {
            pages,
            bitmap,
            dynamic_start,
        })
    }

    pub fn get_mut_ptr<T>(&self) -> *mut T {
        self.pages.get_ptr().as_ptr()
    }

    /// 动态分配 per-CPU 内存
    ///
    /// 分配成功时返回相对每个 CPU unit 起点的动态位置，单位为字节
    pub(super) fn allocate(&self, layout: Layout) -> Result<usize, MemoryError> {
        self.bitmap
            .lock()
            .allocate(layout)
            .map(|bitmap_position| self.dynamic_start + bitmap_position)
    }

    /// 释放动态分配的 per-CPU 内存
    ///
    /// `dynamic_position` 必须是通过 `allocate` 返回的位置
    pub(super) fn deallocate(&self, dynamic_position: usize) -> Result<(), MemoryError> {
        let bitmap_position = dynamic_position - self.dynamic_start;

        self.bitmap.lock().deallocate(bitmap_position).map(|_| ())
    }
}
