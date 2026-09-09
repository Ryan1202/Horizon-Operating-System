//! 固定容量位集合。存储由外部提供，所有位置和长度均以 bit 为单位

use core::num::NonZeroUsize;

const WORD_BITS: usize = usize::BITS as usize;

pub struct BitSet<S> {
    storage: S,
    len: usize,
}

impl<S> BitSet<S> {
    pub const fn len(&self) -> usize {
        self.len
    }

    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    fn check_range(&self, start: usize, count: usize) -> Option<()> {
        if start <= self.len && count <= self.len - start {
            Some(())
        } else {
            None
        }
    }
}

impl<const N: usize> BitSet<[usize; N]> {
    /// 创建固定长度的全零集合，可用于静态初始化。
    pub const fn zeroed(len: usize) -> Self {
        assert!(len.div_ceil(WORD_BITS) <= N, "insufficient bit storage");
        Self {
            storage: [0; N],
            len,
        }
    }
}

impl<S: AsRef<[usize]>> BitSet<S> {
    /// 包装已有存储，不改变其内容；多余 word 和尾部 bit 不参与操作
    pub fn from_storage(storage: S, len: usize) -> Self {
        assert!(
            len.div_ceil(WORD_BITS) <= storage.as_ref().len(),
            "insufficient bit storage"
        );
        Self { storage, len }
    }

    pub fn test(&self, bit: usize) -> bool {
        self.check_range(bit, 1);
        self.storage.as_ref()[bit / WORD_BITS] & (1 << (bit % WORD_BITS)) != 0
    }

    pub fn is_range_clear(&self, start: usize, count: usize) -> bool {
        self.check_range(start, count);
        self.find_in_range(start, count, true).is_none()
    }

    /// 从 start（含）开始查找；start == len 时返回 None。
    pub fn find_zero(&self, start: usize) -> Option<usize> {
        self.check_range(start, 0)?;
        self.find_in_range(start, self.len - start, false)
    }

    pub fn find_one(&self, start: usize) -> Option<usize> {
        self.check_range(start, 0)?;
        self.find_in_range(start, self.len - start, true)
    }

    fn find_in_range(&self, start: usize, count: usize, one: bool) -> Option<usize> {
        let words = self.storage.as_ref();
        for (index, mask) in WordMasks::new(start, count) {
            let word = if one { words[index] } else { !words[index] } & mask;
            if word != 0 {
                return Some(index * WORD_BITS + word.trailing_zeros() as usize);
            }
        }
        None
    }

    /// 查找最早的连续空闲区间；align 必须为非零二次幂，相对于 bit 0。
    /// 数量超过剩余容量返回 None，不属于非法范围。
    pub fn find_zero_range(
        &self,
        start: usize,
        count: NonZeroUsize,
        align: usize,
    ) -> Option<usize> {
        self.check_range(start, 0);
        assert!(align.is_power_of_two(), "invalid bit alignment");
        let mut candidate = start.checked_next_multiple_of(align)?;
        while candidate <= self.len && count.get() <= self.len - candidate {
            // 先按 word 跳过连续占用区域，单 bit 分配也不会逐 bit 扫描。
            candidate = self.find_zero(candidate)?.checked_next_multiple_of(align)?;
            if candidate > self.len || count.get() > self.len - candidate {
                return None;
            }
            match self.find_in_range(candidate, count.get(), true) {
                None => return Some(candidate),
                Some(occupied) => candidate = (occupied + 1).checked_next_multiple_of(align)?,
            }
        }
        None
    }

    pub fn find_first_zero(&self) -> Option<usize> {
        self.find_zero(0)
    }
}

impl<S: AsRef<[usize]> + AsMut<[usize]>> BitSet<S> {
    pub fn set(&mut self, bit: usize) {
        self.try_set(bit);
    }

    pub fn clear(&mut self, bit: usize) {
        self.try_clear(bit);
    }

    /// 尝试设置 bit，仅在原值为 0 时返回 true；非原子操作。
    pub fn try_set(&mut self, bit: usize) -> Option<()> {
        self.check_range(bit, 1)?;
        let word = &mut self.storage.as_mut()[bit / WORD_BITS];
        let mask = 1 << (bit % WORD_BITS);
        let old = *word & mask != 0;
        *word |= mask;
        (!old).then_some(())
    }

    /// 尝试清除 bit，仅在原值为 1 时返回 true；非原子操作。
    pub fn try_clear(&mut self, bit: usize) -> Option<()> {
        self.check_range(bit, 1)?;
        let word = &mut self.storage.as_mut()[bit / WORD_BITS];
        let mask = 1 << (bit % WORD_BITS);
        let old = *word & mask != 0;
        *word &= !mask;
        old.then_some(())
    }

    pub fn set_range(&mut self, start: usize, count: usize) {
        self.change_range(start, count, true);
    }

    pub fn clear_range(&mut self, start: usize, count: usize) {
        self.change_range(start, count, false);
    }

    fn change_range(&mut self, start: usize, count: usize, set: bool) {
        self.check_range(start, count);
        let words = self.storage.as_mut();
        for (index, mask) in WordMasks::new(start, count) {
            if set {
                words[index] |= mask;
            } else {
                words[index] &= !mask;
            }
        }
    }

    /// 占用指定空闲区间。重叠返回 false，且不修改任何 bit；越界 panic。
    pub fn assign(&mut self, start: usize, count: NonZeroUsize) -> bool {
        if !self.is_range_clear(start, count.get()) {
            return false;
        }
        self.set_range(start, count.get());
        true
    }

    pub fn allocate(&mut self, start: usize, count: NonZeroUsize, align: usize) -> Option<usize> {
        let start = self.find_zero_range(start, count, align)?;
        self.set_range(start, count.get());
        Some(start)
    }
}

/// 调用者先检查范围；每次产生一个 word 的有效位掩码。
struct WordMasks {
    index: usize,
    bit: usize,
    remaining: usize,
}

impl WordMasks {
    fn new(start: usize, count: usize) -> Self {
        Self {
            index: start / WORD_BITS,
            bit: start % WORD_BITS,
            remaining: count,
        }
    }
}

impl Iterator for WordMasks {
    type Item = (usize, usize);

    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining == 0 {
            return None;
        }
        let count = self.remaining.min(WORD_BITS - self.bit);
        let mask = (usize::MAX >> (WORD_BITS - count)) << self.bit;
        let index = self.index;
        self.remaining -= count;
        self.index += 1;
        self.bit = 0;
        Some((index, mask))
    }
}
