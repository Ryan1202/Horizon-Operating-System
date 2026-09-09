use core::{alloc::Layout, num::NonZeroUsize};

use alloc::boxed::Box;

use super::bitset::BitSet;
use crate::{
    CACHELINE_SIZE,
    kernel::memory::{MemoryError, kmalloc::Kmalloc},
};

const WORD_BITS: u32 = usize::BITS;
const MAX_ALLOC_SIZE: usize = 16 * 1024; // 16 KiB

struct AllocMap<'a> {
    alloc_map: BitSet<&'a mut [usize]>,
    bound_map: BitSet<&'a mut [usize]>,
}

/// 按 `UNIT_SIZE` 字节单元管理内存的位图分配器。
///
/// 两组位图连续存放于一次堆分配中，每组最多 `u16::MAX` 个 word；
/// 边界位图额外记录容量末尾的边界，因此最多管理
/// `u16::MAX * usize::BITS - 1` 个单元，字节容量还须能用 `u32` 表示。
pub struct Bitmap<const UNIT_SIZE: usize> {
    array: Box<[usize], Kmalloc>,
    size: u32,
    alloc_words: u16,
    /// 第一个空闲单元的下标；没有空闲单元时等于单元总数。
    first_free: u32,
}

impl<const UNIT_SIZE: usize> Bitmap<UNIT_SIZE> {
    pub fn try_new(size: u32) -> Result<Self, MemoryError> {
        if !UNIT_SIZE.is_power_of_two() || UNIT_SIZE > CACHELINE_SIZE {
            return Err(MemoryError::ViolateConstraint);
        }
        if size == 0 || !size.is_multiple_of(UNIT_SIZE as u32) {
            return Err(MemoryError::InvalidSize(size as usize));
        }

        let units = size / UNIT_SIZE as u32;
        let boundary_bits = units
            .checked_add(1)
            .ok_or(MemoryError::InvalidSize(size as usize))?;
        let alloc_words = u16::try_from(boundary_bits.div_ceil(WORD_BITS))
            .map_err(|_| MemoryError::InvalidSize(size as usize))?;
        let total_words = usize::from(alloc_words) * 2;

        let array = Box::new_zeroed_slice_in(total_words, Kmalloc::default());
        // SAFETY: usize 的全零位模式有效，整个数组已由分配器清零。
        let array = unsafe { array.assume_init() };

        Ok(Self {
            array,
            size,
            alloc_words,
            first_free: 0,
        })
    }

    pub fn allocate(&mut self, layout: Layout) -> Result<usize, MemoryError> {
        let size = layout.size();
        if size == 0 || size > MAX_ALLOC_SIZE {
            return Err(MemoryError::InvalidSize(size));
        }

        let align = layout.align().max(UNIT_SIZE);
        if align > CACHELINE_SIZE {
            return Err(MemoryError::ViolateConstraint);
        }

        let count = NonZeroUsize::new(size.div_ceil(UNIT_SIZE)).unwrap();
        let capacity = self.size as usize / UNIT_SIZE;

        let mut maps = AllocMap::new(&mut self.array, self.alloc_words, capacity);

        let start = maps
            .alloc_map
            .allocate(self.first_free as usize, count, align / UNIT_SIZE)
            .ok_or(MemoryError::OutOfMemory)?;

        maps.mark_boundaries(start, count.get());
        maps.advance_first_free(start, count.get(), &mut self.first_free);

        Ok(start * UNIT_SIZE)
    }

    /// 从单元下标 `start_unit` 开始分配 `count` 个单元，不受单次 allocate 大小限制
    ///
    /// 无效起点返回 `InvalidAllocationOffset(start_unit)`（载荷是单元下标）。
    /// 范围超出容量或已有占用返回 `OutOfMemory`，失败不修改位图。
    /// 成功后使用 `deallocate(start_unit * UNIT_SIZE)` 释放
    pub fn assign(&mut self, start_unit: usize, count: NonZeroUsize) -> Result<(), MemoryError> {
        let capacity = self.size as usize / UNIT_SIZE;
        if start_unit >= capacity {
            return Err(MemoryError::InvalidAllocationOffset(start_unit));
        }
        if count.get() > capacity - start_unit {
            return Err(MemoryError::OutOfMemory);
        }

        let mut maps = AllocMap::new(&mut self.array, self.alloc_words, capacity);
        if !maps.alloc_map.assign(start_unit, count) {
            return Err(MemoryError::OutOfMemory);
        }

        maps.mark_boundaries(start_unit, count.get());
        maps.advance_first_free(start_unit, count.get(), &mut self.first_free);

        Ok(())
    }

    /// 释放以字节偏移 `offset` 为起点的完整分配。
    pub fn deallocate(&mut self, offset: usize) -> Result<(), MemoryError> {
        if offset >= self.size as usize || !offset.is_multiple_of(UNIT_SIZE) {
            return Err(MemoryError::InvalidAllocationOffset(offset));
        }

        let unit = offset / UNIT_SIZE;
        let mut maps = AllocMap::new(
            &mut self.array,
            self.alloc_words,
            self.size as usize / UNIT_SIZE,
        );

        if !maps.alloc_map.test(unit) || !maps.bound_map.test(unit) {
            return Err(MemoryError::InvalidAllocationOffset(offset));
        }

        let end = maps
            .bound_map
            .find_one(unit + 1)
            .expect("missing allocation end boundary");

        maps.alloc_map.clear_range(unit, end - unit);
        self.first_free = self.first_free.min(unit as u32);

        Ok(())
    }
}

impl<'a> AllocMap<'a> {
    fn new(array: &'a mut [usize], alloc_words: u16, units: usize) -> Self {
        let (alloc_map, bound_map) = array.split_at_mut(alloc_words as usize);
        Self {
            alloc_map: BitSet::from_storage(alloc_map, units),
            bound_map: BitSet::from_storage(bound_map, units + 1),
        }
    }

    fn mark_boundaries(&mut self, start: usize, count: usize) {
        self.bound_map.clear_range(start, count);
        self.bound_map.set(start);
        self.bound_map.set(start + count);
    }

    fn advance_first_free(&self, start: usize, count: usize, first_free: &mut u32) {
        if start <= *first_free as usize && (*first_free as usize) < start + count {
            *first_free = self
                .alloc_map
                .find_zero(start + count)
                .unwrap_or(self.alloc_map.len()) as u32;
        }
    }
}
