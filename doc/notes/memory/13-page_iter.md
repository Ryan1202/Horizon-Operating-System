# 页表迭代器

我在 `PageLock` 的基础上，为页表的遍历操作实现了一个迭代器

## 结构

首先定义了每一步的返回结果，对于页表条目来说只有：不存在、指向页表、指向（大）页几种情况

```rust
#[derive(Clone, Copy)]
pub enum PtStep {
    Absent {
        page: PageNumber,
        level: usize,
    },
    Table {
        /// 当前页表内的起始页号
        base: PageNumber,
        /// 页表页的物理页号
        frame: FrameNumber,
        level: usize,
    },
    Leaf {
        page: PageNumber,
        frame: FrameNumber,
        level: usize,
    },
}
```

然后是迭代器的结构

```rust
pub struct PageTableIter<'a, T, L>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T>,
{
    lock: PageLock<'a, T, L>,
    index: usize,
    end: PageNumber,
}
```

非常简单，就是一个页表锁、当前的条目索引，以及结束位置

## 实现

```rust
impl<'a, T, L> PageTableIter<'a, T, L>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T>,
{
    pub fn new(
        root: PtPage<T>,
        page_start: PageNumber,
        page_end: PageNumber,
        phy2vir: fn(FrameNumber) -> *const T,
    ) -> Option<Self> {
        let lock = PageLock::lock_page(root, page_start, phy2vir)?;
        let level = lock.level()?;
        let index = T::entry_index(page_start, level);

        Some(Self {
            lock,
            index,
            end: page_end,
        })
    }

    pub fn get_mut(&mut self) -> Option<&mut T> {
        self.lock.get_mut()
    }

    pub fn get_lock(&self) -> &PageLock<'a, T, L> {
        &self.lock
    }
}
```

这些工具函数基本都是页表锁的简单封装

### next

`next` 会相对复杂一点，毕竟涉及页表条目内容的判断和页表之间的切换

```rust
impl<'a, T, L> Iterator for PageTableIter<'a, T, L>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T>,
{
    type Item = PtStep;

    fn next(&mut self) -> Option<Self::Item> {
        let mut base = self.lock.base();
        let mut level = self.lock.level()?;

        if self.index >= 1 << T::INDEX_BITS[self.lock.level()?] {
            let (_, level) = self.lock.pop()?;

            let page = base + (self.index << T::LEVEL_SHIFTS[level]);
            self.index = T::entry_index(page, level);

            let entry = self.lock.get()?.get_entry(self.index).read();
            let frame = entry.frame_number()?;

            self.index += 1;

            return Some(PtStep::Table { base, frame, level });
        }

        // ...
    }
}
```

在每次调用 `next` 获取下一个条目的时候，都需要判断是否切换到了另一个页表，这里将指向页表的条目类型在退出页表页时返回，因为 `PageLock` 是自动锁到最底层的，这样会比较顺

然后是正常读取条目的部分

```rust
		loop {
            let page = base + (self.index << T::LEVEL_SHIFTS[level]);
            if page >= self.end {
                return None;
            }

            let entry_ptr = self.lock.get()?.get_entry(self.index);
            let entry = entry_ptr.read();
            self.index += 1;

            let Some(frame) = entry.frame_number() else {
                return Some(PtStep::Absent { page, level });
            };
            if !entry.is_present() {
                return Some(PtStep::Absent { page, level });
            }

            if level == 0 || entry.is_huge(level as u8) {
                return Some(PtStep::Leaf { page, frame, level });
            }

            self.lock.push(self.index - 1, None)?;
            self.index = 0;

            base = self.lock.base();
            level = self.lock.level()?;
        }
```

首先检查如果到达了设定的停止位置，直接返回 `None` ，否则继续读取条目内容

由于页表条目中的地址强制对齐页大小，所以我将其直接抽象成了 `frame_number()` 接口，如果不是有效的则直接视作不存在返回（在目前的实现代码中等同于是否存在）。当然还是补上了一个专门的存在位检测，避免后续实现变动

对于指向具体页的条目，需要依赖页层级检测是基础的 4KB 页或是大页，这两种页统一用 `PtStep::Leaf` 表示

这里在最外层套上了死循环 `loop` ，因为如果遇到了页表需要进入其中重新读取，最后几行就是在更新相关数据。`push` 的时候 `self.index - 1` 是因为前面默认跳到同级的下一个索引了，而进入页表需要使用旧索引

# 使用示例

## 地址翻译

```rust
/// 翻译虚拟地址到物理地址
pub fn translate(root_pt: *const usize, vaddr: VirtAddr) -> Option<PhysAddr>
where
    for<'a> NormalPtLock: PtRwLock<'a, T>,
{
    let page = vaddr.to_page_number();
    let root = PtPage::from_ptr(root_pt as *const T);
    let mut iter =
        PageTableIter::<T, NormalPtLock>::new(root, page, page + 1, linear_table_ptr::<T>)?;

    let step = iter.next()?;

    if let PtStep::Leaf { frame, level, .. } = step {
        let offset =
            vaddr.as_usize() & ((1 << (T::LEVEL_SHIFTS[level] + T::PAGE_OFFSET_BITS)) - 1);
        let phys = PhysAddr::from_frame_number(frame).as_usize() + offset;
        Some(PhysAddr::new(phys))
    } else {
        None
    }
}
```

## 遍历所有存在的物理页元数据

```rust
let leaf_iter = PageTableIter::<ArchPageTable, NormalPtLock>::new(
    PtPage::from_ptr(root_pt as *const ArchPageTable),
    start,
    VMEMMAP_END.to_page_number(),
    linear_table_ptr::<ArchPageTable>,
)
.expect("failed to iterate vmemmap mappings")
.filter_map(|x| match x {
    PtStep::Leaf { page, .. } => Some(page),
    _ => None,
});

for page in leaf_iter {
    // 计算帧号
    let num = (page.get() - VMEMMAP_BASE.to_page_number().get()) * VMAP_PER_PAGE;

    let range_start = FrameNumber::new(num);
    let range_end = FrameNumber::new((num + 1).next_multiple_of(VMAP_PER_PAGE));
    
    // ...
}
```

