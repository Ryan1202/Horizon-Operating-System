use core::alloc::Layout;

use alloc::boxed::Box;

use crate::{
    CACHELINE_SIZE,
    kernel::memory::{MemoryError, kmalloc::Kmalloc},
};

const WORD_BITS: u32 = usize::BITS;

const MAX_ALLOC_SIZE: u32 = 16 * 1024; // 16 KiB

const fn word_count(bits: u32) -> u32 {
    bits.div_ceil(WORD_BITS)
}

struct AllocMap<'a, const UNIT_SIZE: usize> {
    alloc_map: &'a mut [usize],
    bound_map: &'a mut [usize],
}

/// `Bitmap` 是一个位图分配器，用于管理固定大小的内存块
///
/// `Bitmap` 最大支持管理 4 GB 的内存.最多可以使用 65536 个 `usize` 来管理,
/// 即在 32 位系统上最多可以管理 `2^21` 个单位，在 64 位系统上最多可以管理 `2^22` 个单位
///
/// `UNIT_SIZE` 是分配器管理的最小单位的大小，单位为字节
pub struct Bitmap<const UNIT_SIZE: usize> {
    array: Box<[usize], Kmalloc>,
    size: u32,
    alloc_words: u16,
    /// 第一个空闲分配单元的下标，不是位图 word 的下标
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
        let alloc_words = word_count(units + 1) as u16;
        let bound_words = alloc_words;
        let total_words = usize::from(alloc_words) + usize::from(bound_words);

        let array = Box::try_new_zeroed_slice_in(total_words, Kmalloc::default())
            .map_err(|_| MemoryError::OutOfMemory)?;
        // SAFETY: usize 的全零位模式有效，且分配器已返回完整初始化前的零填充数组
        let array = unsafe { array.assume_init() };
        Ok(Self {
            array,
            size,
            alloc_words,
            first_free: 0,
        })
    }

    pub fn allocate(&mut self, layout: Layout) -> Result<usize, MemoryError> {
        let mut alloc_map = AllocMap::<UNIT_SIZE>::new(&mut self.array, self.alloc_words);

        alloc_map.allocate(layout, self.size, &mut self.first_free)
    }

    pub fn deallocate(&mut self, offset: usize) -> Result<(), MemoryError> {
        if offset >= self.size as usize || !offset.is_multiple_of(UNIT_SIZE) {
            return Err(MemoryError::InvalidAllocationOffset(offset));
        }
        let unit = (offset / UNIT_SIZE) as u32;
        let mut alloc_map = AllocMap::<UNIT_SIZE>::new(&mut self.array, self.alloc_words);

        alloc_map.deallocate(unit, &mut self.first_free)
    }
}

impl<'a, const UNIT_SIZE: usize> AllocMap<'a, UNIT_SIZE> {
    fn new(array: &'a mut [usize], alloc_words: u16) -> Self {
        let (alloc_map, bound_map) = array.split_at_mut(alloc_words as usize);
        Self {
            alloc_map,
            bound_map,
        }
    }

    fn allocate(
        &mut self,
        layout: Layout,
        capacity: u32,
        first_free: &mut u32,
    ) -> Result<usize, MemoryError> {
        let size = layout.size();
        if size == 0 || size > MAX_ALLOC_SIZE as usize {
            return Err(MemoryError::InvalidSize(size));
        }
        let size = size as u32;
        let units = size.div_ceil(UNIT_SIZE as u32);

        let align = layout.align().max(UNIT_SIZE);
        if align > CACHELINE_SIZE {
            return Err(MemoryError::ViolateConstraint);
        }
        let unit_align = (align / UNIT_SIZE) as u32;

        let mut start = first_free.next_multiple_of(unit_align);
        let capacity_units = capacity / UNIT_SIZE as u32;

        while start + units <= capacity_units {
            let offset = start * UNIT_SIZE as u32;
            let bit_index = BitIndex::new(start);
            if self.test(&bit_index, units) {
                self.mark_allocated(&bit_index, units);

                if start == *first_free {
                    *first_free = start + units;
                }
                return Ok(offset as usize);
            }

            start = (start + units).next_multiple_of(unit_align);
        }

        Err(MemoryError::OutOfMemory)
    }

    fn test(&self, bit_index: &BitIndex, len: u32) -> bool {
        let mut remaining = len;
        let mut bit = bit_index.bit;
        let mut index = bit_index.index;
        while remaining > 0 {
            let len = remaining.min(WORD_BITS - bit);
            let mask = (usize::MAX >> (WORD_BITS - len)) << bit;
            if self.alloc_map[index as usize] & mask != 0 {
                return false;
            }
            remaining -= len;
            index += 1;
            bit = 0;
        }
        true
    }

    fn mark_allocated(&mut self, bit_index: &BitIndex, len: u32) {
        let mut remaining = len;
        let mut bit = bit_index.bit;
        let mut index = bit_index.index;
        while remaining > 0 {
            let len = remaining.min(WORD_BITS - bit);
            let mask = (usize::MAX >> (WORD_BITS - len)) << bit;
            self.alloc_map[index as usize] |= mask;
            self.bound_map[index as usize] &= !mask;
            remaining -= len;
            index += 1;
            bit = 0;
        }

        self.bound_map[bit_index.index as usize] |= 1 << bit_index.bit;
        let end = bit_index.bit + len;
        let end_index = bit_index.index + end / WORD_BITS;
        self.bound_map[end_index as usize] |= 1 << (end % WORD_BITS);
    }

    fn deallocate(&mut self, unit: u32, first_free: &mut u32) -> Result<(), MemoryError> {
        let bitmask = BitIndex::new(unit);
        if self.alloc_map[bitmask.index as usize] & (1 << bitmask.bit) == 0
            || self.bound_map[bitmask.index as usize] & (1 << bitmask.bit) == 0
        {
            return Err(MemoryError::InvalidAllocationOffset(
                unit as usize * UNIT_SIZE,
            ));
        }

        let end = self.find_boundary(&bitmask);
        self.mark_free(&bitmask, &end);

        *first_free = (*first_free).min(unit);

        Ok(())
    }

    fn find_boundary(&self, start: &BitIndex) -> BitIndex {
        let bit = (start.bit + 1) % WORD_BITS;
        let mut index = start.index + (start.bit + 1) / WORD_BITS;
        let mut word = self.bound_map[index as usize] & (usize::MAX << bit);
        while word.highest_one().is_none() {
            index += 1;
            word = self.bound_map[index as usize];
        }
        BitIndex {
            index,
            bit: word.lowest_one().unwrap(),
        }
    }

    fn mark_free(&mut self, start: &BitIndex, end: &BitIndex) {
        let mut remaining = end.position() - start.position();
        let mut bit = start.bit;
        let mut index = start.index;
        while remaining > 0 {
            let len = remaining.min(WORD_BITS - bit);
            let mask = (usize::MAX >> (WORD_BITS - len)) << bit;
            self.alloc_map[index as usize] &= !mask;
            remaining -= len;
            index += 1;
            bit = 0;
        }
    }
}

struct BitIndex {
    index: u32,
    bit: u32,
}

impl BitIndex {
    const fn new(position: u32) -> Self {
        let index = position / WORD_BITS;
        let bit = position % WORD_BITS;
        Self { index, bit }
    }

    const fn position(&self) -> u32 {
        self.index * WORD_BITS + self.bit
    }
}
