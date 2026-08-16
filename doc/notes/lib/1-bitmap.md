目前的 `Bitmap` 是为动态的 per-CPU 分配而设计的，不同于 SLUB 都是分配相同的大小，`Bitmap` 需要在同一个区域满足不同的大小以及对齐要求

# 类型定义

`Bitmap` 类型定义如下：

```rust
/// `Bitmap` 是一个位图分配器，用于管理固定大小的内存块。
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
```

为了将 `Bitmap` 大小减小，将可管理的大小上限设为了 $2^{32}$ 字节，相应的字数量也限制在了 $2^{16}$，只要不是作为全部内存的分配器是完全足够的

使用 `Box` 来管理真正的位图 `array`，因为一方面位图的大小是不确定的，需要通过智能指针才能安全使用 `!Sized` 的类型，另一方面智能指针也省去了手工管理内存的麻烦

`array` 实际上包含了两个逻辑上的位图，一个用来标识每个部分是否被分配，另一个用于标识分配的边界从而只需要传入起始位置即可释放。放在一起是为了防止使用两个 ` Box` 造成额外的空间开销，实际通过 `AllocMap` 来使用

```rust
struct AllocMap<'a, const UNIT_SIZE: usize> {
    alloc_map: &'a mut [usize],
    bound_map: &'a mut [usize],
}
```

# AllocMap

分配的核心逻辑都在 `AllocMap` 中

## 创建

```rust
fn new(array: &'a mut [usize], alloc_words: u16) -> Self {
    let (alloc_map, bound_map) = array.split_at_mut(alloc_words as usize);
    Self {
        alloc_map,
        bound_map,
    }
}
```

通过 `split_at_mut` 将 `array` 分成两个 map

---

接下来的是核心的分配和释放逻辑

### 预处理

首先将输入的 `Layout` 中提取出 `size` 和 `align` ，并处理成以 `UNIT_SIZE` 为单位

```rust
let size = layout.size() as u32;
if size == 0 || size > MAX_ALLOC_SIZE {
    return Err(MemoryError::InvalidSize(size as usize));
}
let units = size.div_ceil(UNIT_SIZE as u32);

let align = (layout.align().max(UNIT_SIZE) / UNIT_SIZE) as u32;
if align
    .checked_mul(UNIT_SIZE as u32)
    .is_none_or(|align| align > CACHELINE_SIZE as u32)
{
    return Err(MemoryError::ViolateConstraint);
}
```

## 初始状态

```rust
let unit_align = (align / UNIT_SIZE) as u32;

let mut start = first_free.next_multiple_of(unit_align);
let capacity_units = capacity / UNIT_SIZE as u32;
```

最开始就将位置对齐到 `align` ，之后就是按大小和对齐要求循环查找

## 查找并分配

```rust
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
```

找到时顺便更新 `first_free` 加速下一次查找

## 检测

其中 `test` 用来检测指定范围内是否全为 `0`

```rust
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
```

## 更新标记

`alloc_map` 不必多说，`bound_map` 用来标记边界，在每个一分配区域的开头位置置 `1`，同时为了标定结尾位置，会将结尾的下一位也置 `1`

```rust
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
```

类似的还有释放时用的 `mark_free`

```rust
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
```

## 释放

```rust
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
```

很简单，就是查找边界，更新标记，顺便更新 `first_free`

## 查找边界

确定了边界的规则其实很好写：

```rust
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
```

使用了 `highest_one` 和 `lowest_one` 来加速，这两种操作在 x86 下都有专门的指令加速，其他架构不清楚

# Bitmap

## 创建

这是 `Bitmap` 最复杂的函数，主要是将以字节为单位的数据转换成以 `unit` 和 `word` 为单位的数据，然后使用 `Box` 分配

```rust
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
    // SAFETY: usize 的全零位模式有效，且分配器已返回完整初始化前的零填充数组。
    let array = unsafe { array.assume_init() };
    Ok(Self {
        array,
        size,
        alloc_words,
        first_free: 0,
    })
}
```

## 分配和释放

就只是调用 `AllocMap`

```rust
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
```

