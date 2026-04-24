既然底层的页表相关接口变了，那么上层的相关操作也需要重新写了

# 内存映射

## map_range

内存映射有一个基础接口 `map_range` 

```rust
fn map_range<'a, Lock>(
    lock: &mut PageLock<'a, ArchPageTable, Lock>,
    page_start: PageNumber,
    page_end: PageNumber,
    frame: FrameNumber,
    flags: PageFlags,
    allocate_table: &mut impl FnMut() -> Result<
        (FrameNumber, *const ArchPageTable, Option<Lock::Table>),
        PageTableError,
    >,
) -> Result<(), PageTableError>
where
    Lock: 'a + PtRwLock<'a, ArchPageTable>,
{
    type Entry = <ArchPageTable as PageTable>::Entry;

    let mut current_page = page_start;
    let mut current_frame = frame;
    
    let mut base = lock.base();
    let mut level = lock.level().ok_or(PageTableError::InvalidLevel)?;

    let mut index = ArchPageTable::entry_index(current_page, level);
    while current_page < page_end {
        let start = current_page.max(base);
        let end = page_end.min(base + ArchPageTable::LEVEL_COUNTS[level + 1]);

        assert!(start < end);
        
        // ...
    }
}
```

前面的部分很简单，根据给定的范围进行循环，每一次循环都计算当前的状态信息（页表层级、索引、在当前页表中的起止位置等等）

```rust
		let count = end.get() - start.get();
        let use_huge = (current_frame.get() & !ArchPageTable::LEVEL_MASKS[level]) == 0
            && count >= ArchPageTable::LEVEL_COUNTS[level]
            && ArchPageTable::HUGE_PAGE[level];
```

然后根据映射的范围大小和地址对齐判断是否能够使用大页

```rust
        let entry = lock.get().unwrap()[index].read();
        if entry.is_present() {
            if use_huge || level == 0 || entry.is_huge(level as u8) {
                return Err(PageTableError::EntryAlreadyMapped);
            } else {
                lock.push(index, None).unwrap();
                continue;
            }
        }
```

读取当前指向的条目，如果存在映射，有两种情况：

- 指向普通页：冲突，返回
- 指向页表页：进入，回到头部更新状态

```rust
		let step = ArchPageTable::LEVEL_COUNTS[level];
        // 当前页表增加引用
        mem::forget(lock.clone_current());

        let entry_ptr = &mut lock.get_mut().unwrap()[index];

        let entry = if use_huge {
            Entry::new_mapped(current_frame, flags, level)
        } else if level == 0 {
            Entry::new_mapped(current_frame, flags.huge_page(false), level)
        } else {
            let (table_frame, _, table) = allocate_table()?;

            let flags = flags.huge_page(false).cache_type(PageCacheType::WriteBack);

            entry_ptr.write(Entry::new_mapped(table_frame, flags, level));

            lock.push(index, table).unwrap();

            base = lock.base();
            level = lock.level().unwrap();
            index = ArchPageTable::entry_index(current_page, level);
            continue;
        };

        entry_ptr.write(entry);
```

因为增加了新条目，所以当前的页表页就增加了一个引用。填入的条目可能是普通页或者页表页，如果是普通页则直接写入，如果是页表页则需要现分配一个页表页写入，然后再 `push` 到页表锁里。

由于普通页需要更新 `current_page` 和 `current_frame` ，而且页表页需要先写入条目再 `push` ，所以分成了两条路径

```rust
		while index + 1 >= (1 << ArchPageTable::INDEX_BITS[level]) {
            lock.pop().ok_or(PageTableError::InvalidLevel)?;
            base = lock.base();
            level = lock.level().unwrap();
            index = ArchPageTable::entry_index(current_page, level);
        }
        index += 1;

        current_page += step;
        current_frame += step;
```

最后在切换到下一个 `index` 时检测一下是否离开了当前页表，如果是则循环 `pop` 并更新页状态。最后更新 `index` , `current_page` , `current_frame`

## 建立映射

在 `map_range` 的基础上，通过 `PageTableOps<T>` trait 提供了一个对外的接口 `map`

```rust
/// 将虚拟页映射到物理页帧
///
/// 如果中间级页表不存在，使用 `frame_alloc` 分配新的页表帧。
/// 如果目标 PTE 已经 present，返回 `EntryAlreadyMapped`。
pub fn map(
    root_pt: *const usize,
    pages: &mut DynPages,
    offset: usize,
    frames: &mut UniqueFrames,
    flags: PageFlags,
) -> Result<(), PageTableError> {
    let page_count = frames.order().to_count().get();

    let page_start = pages.start_addr().to_page_number() + offset;
    let page_end = page_start + page_count;

    let mut allocate_table = || alloc_table::<ArchPageTable>();

    let root = PtPage::from_ptr(root_pt as *const ArchPageTable);
    let mut lock = PageLock::<ArchPageTable, NormalPtLock>::lock_page(
        root,
        page_start,
        linear_table_ptr::<ArchPageTable>,
    )
    .ok_or(PageTableError::EntryAbsent)?;

    map_range::<NormalPtLock>(
        &mut lock,
        page_start,
        page_end,
        frames.frame_number(),
        flags,
        &mut allocate_table,
    )
}
```

除开 `map_range` 基本上就只是在做一些数据预处理，严格限制传入的类型 `DynPages` 和 `UniqueFrames` ，`root_pt` 在发生进程切换时会变化，所以暂时也是依赖外部输入，映射范围依赖 `frames` 的 `order` 属性确保不会大小是匹配的。

首先将 `order` 转换为页数，计算页号的起止范围；然后将分配页表的函数转换成闭包用于传递给 `map_range` ；再之后就是根据传入的根页表和虚拟地址锁上页表锁。

## 删除条目

`unmap_entry` 用来将某个条目删除，需要注意的是 `unmap_entry` 不负责处理引用计数

```rust
fn unmap_entry<'a, T, L>(
    lock: &mut PageLock<'a, T, L>,
    page: PageNumber,
) -> Result<(FrameNumber, usize), PageTableError>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T, Table = SharedFrames> + 'a,
{
    let level = lock.level().ok_or(PageTableError::InvalidLevel)?;
    let index = T::entry_index(page, level);
    let entry = lock.get().ok_or(PageTableError::EntryAbsent)?[index].read();
    let Some(frame_number) = entry.frame_number() else {
        return Err(PageTableError::EntryAbsent);
    };

    lock.get_mut().ok_or(PageTableError::EntryAbsent)?[index].write(T::Entry::new_absent());

    Ok((frame_number, T::LEVEL_COUNTS[level]))
}
```

通过传入的页表锁引用获取到当前所指向的条目，然后获取其可变引用并覆盖为 “不存在” 的状态

## 减少页表引用计数

听起来是很简单的操作，但是由于涉及页表的自动释放，还是有点复杂的，因为在已经持有页表锁的状态下通过 `SharedFrames` 的 `drop` 路径自动释放会导致死锁，而且重写加锁也会有一定的开销。

我把它拆成了几个简单的操作：

首先是最基础的释放页表，两种路径都会调用

```rust
/// 释放页表，调用前请先 pop 页表锁，保证指向要释放的页表的父页表
///
/// # Safety
///
/// 需要确保 index 指向正确的页表条目
pub unsafe fn drop_table<'a>(
    lock: &mut PageLock<'a, ArchPageTable, NormalPtLock>,
    index: usize,
) -> Option<()> {
    lock.get_mut()?[index].write(<ArchPageTable as PageTable>::Entry::new_absent());

    let frame = lock.get_frame()?;
    frame.refcount.release();

    Some(())
}
```

很简单的覆盖条目，然后减少父页表页的引用计数

然后再包装一下，顺便处理页表页之间的关系，批量取消映射时会用到

```rust
fn trim_table<'a>(
    lock: &mut PageLock<'a, ArchPageTable, NormalPtLock>,
    page: PageNumber,
) -> Option<()> {
    loop {
        // 如果当前页表页还有其他映射则直接返回
        let count = unsafe { lock.drop_current()? };
        if count > 1 {
            return Some(());
        }

        // 否则弹出当前页表页引用以释放
        let (table, level) = lock.pop()?;

        if let Some(mut unique) = table.try_upgrade() {
            let index = ArchPageTable::entry_index(page, level);

            unsafe { drop_table(lock, index)? };

            // 走普通页的释放路径而不是页表页的特殊路径，避免死锁
            Anonymous::new(unique.order()).replace_frame(&mut unique);
            drop(unique);
        } else {
            return Some(());
        }
    }
}
```

这里除了减少引用计数外，释放时直接将其变成了 `Anonymous` 页，因为还保持 `PageTable` 类型的话会触发 `drop` 里处理页表页的路径，我们需要避免这一点，自己处理释放

## 取消映射

最后封装成对外的取消映射接口 `unmap` 

```rust
/// 取消虚拟页的映射
///
/// 返回之前映射的物理页帧。调用者根据需要调用 TLB flush。
pub fn unmap(
    root_pt: *const usize,
    pages: &mut DynPages,
    offset: usize,
    order: FrameOrder,
) -> Result<FrameNumber, PageTableError> {
    let mut page_number =
        (pages.start_addr() + offset * ArchPageTable::PAGE_SIZE).to_page_number();
    let end = page_number + order.to_count().get();
    let root = PtPage::from_ptr(root_pt as *const ArchPageTable);
    let mut head = None;

    let mut lock = PageLock::<ArchPageTable, NormalPtLock>::lock_page(
        root,
        page_number,
        linear_table_ptr::<ArchPageTable>,
    )
    .ok_or(PageTableError::EntryAbsent)?;

    while page_number < end {
        let (frame_number, count) = unmap_entry(&mut lock, page_number)?;
        trim_table(&mut lock, page_number).ok_or(PageTableError::InvalidLevel)?;

        page_number = page_number + count;

        if head.is_none() {
            head = Some(frame_number);
        }
    }

    head.ok_or(PageTableError::EntryAbsent)
}
```

这里也用类型来限制传入的参数，然后通过循环串起 `unmap_entry` 和 `trim_table` 来覆盖的所有处理条目和页表页

## 早期映射

最后还有一个内核初始化早期用的映射函数，此时页元数据、锁都还没初始化，我利用泛型服用了代码，将直接的内存操作封装成了 `EarlyPtLock` 

```rust
/// 在内核早期阶段映射虚拟页到物理页帧
///
/// # Safety
///
/// 只能在单核环境下使用，且调用者必须保证映射范围不重叠且线性地址空间足够。
pub unsafe fn early_map(
    root_pt: *const usize,
    page: PageNumber,
    frame: FrameNumber,
    count: usize,
    flags: PageFlags,
) -> Result<(), PageTableError> {
    let end = page + count;

    let mut allocate_table = || {
        let frame = early_allocate_pages(1).to_frame_number();
        let ptr = if frame.get() < LINEAR_END.load(Ordering::Relaxed) {
            linear_table_ptr::<ArchPageTable>(frame)
        } else {
            kernel_table_ptr::<ArchPageTable>(frame)
        };
        unsafe { (ptr as *mut usize).write_bytes(0, T::PAGE_SIZE) };
        Ok((frame, ptr, Some(())))
    };

    fn phy2vir<T: PageTable>(frame: FrameNumber) -> *const T {
        if frame.get() < LINEAR_END.load(Ordering::Relaxed) {
            linear_table_ptr::<T>(frame)
        } else {
            kernel_table_ptr::<T>(frame)
        }
    }

    let root = root_pt as *mut ArchPageTable;
    let root = PtPage::from_ptr(root);

    let mut lock = PageLock::<ArchPageTable, EarlyPtLock>::lock_page(root, page, phy2vir)
        .ok_or(PageTableError::EntryAbsent)?;

    map_range::<EarlyPtLock>(&mut lock, page, end, frame, flags, &mut allocate_table)
}
```

这里自定义了一个页表分配函数和物理地址到虚拟地址到转换，默认了页表页需要在线性映射范围内，甚至基于启动早期的 bump allocator 的特性直接使用内核代码区的映射来建立最早的线性映射区映射

# 其他

## 地址转换

很简单，通过页表迭代器去找就行了

```rust
/// 翻译虚拟地址到物理地址
pub fn translate(root_pt: *const usize, vaddr: VirtAddr) -> Option<PhysAddr>
where
    for<'a> NormalPtLock: PtRwLock<'a, T>,
{
    const LINEAR_RANGE: Range<usize> = KLINEAR_BASE.as_usize()..KLINEAR_END.as_usize();
    const KERNEL_RANGE: Range<usize> = KERNEL_BASE.as_usize()..(KERNEL_END.as_usize());

    if LINEAR_RANGE.contains(&vaddr.as_usize()) {
        let offset = vaddr.as_usize() - KLINEAR_BASE.as_usize();
        return Some(PhysAddr::new(offset));
    } else if KERNEL_RANGE.contains(&vaddr.as_usize()) {
        let offset = vaddr.as_usize() - KERNEL_BASE.as_usize();
        return Some(PhysAddr::new(offset));
    }

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

这里做了一个简单的小优化，将线性映射区和内核代码区的地址直接通过计算返回不用查表

## 修改映射的flag

```rust
/// 修改已有映射的 flags
pub fn update_flags(root_pt: *const usize, pages: &mut DynPages, flags: PageFlags) -> Option<()>
where
    for<'a> NormalPtLock: PtRwLock<'a, T>,
{
    let root = PtPage::from_ptr(root_pt as *const T);
    let page_number = pages.start_addr().to_page_number();

    let mut page_lock =
        PageLock::<T, NormalPtLock>::lock_page(root, page_number, linear_table_ptr::<T>)?;

    let level = page_lock.level()?;
    let index = T::entry_index(page_number, level);
    let table = page_lock.get_mut()?;
    let entry_ptr = &mut table[index];

    let mut entry = entry_ptr.read();
    entry.set_flags(flags, level);
    entry_ptr.write(entry);

    Some(())
}
```

也不复杂，不解释了

