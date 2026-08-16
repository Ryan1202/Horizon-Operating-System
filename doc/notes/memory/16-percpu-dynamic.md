# Chunk

为了保证 per-CPU 变量在所有核心上都能使用同一个 handle ，即 `.data..percpu` 到变量模板地址的偏移，每个核心的变量地址到开头的偏移都要相同。如果只是连续分配的内存（虚拟地址连续即可）那么无需特别处理即可满足，但如果需要动态扩展大小的话就无法保证各个核心新分配的区域到上一个区域到偏移相同了。因此，引入了 `Chunk` 的管理单位，所有的 CPU 核心分配的时候合并在一起分配

```rust
pub struct PerCpuChunk {
    pages: Pages,
    bitmap: Spinlock<Bitmap<{ ALLOC_UNIT }>>,

    /// 动态分配区相对每个 CPU unit 起点的位置，单位为字节
    dynamic_start: usize,
}
```

`ALLOC_UNIT` 是一个常量，是 `Bitmap` 管理的分配最小单位。`bitmap` 仅仅是管理结构，通过 `pages` 持有分配的页的所有权

然后就是核心的分配和释放接口，使用 offset 作为 handle

```rust
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
```

# Area

所有 per-CPU 变量使用的区域使用 `PerCpuArea` 来管理，目前先只使用一个 `Chunk` 来简化

```rust
pub struct PercpuArea {
    first_chunk: PerCpuChunk,
    /// 当前 per-CPU 区域的 CPU 核心数量
    count: usize,
    /// 每个 CPU unit 的实际大小，单位为字节
    unit_size: usize,
}
```

初始化部分很简单，分配内存、复制模板、初始化管理机构

```rust
pub fn try_new(frame: UniqueFrames, nr_cpu_limit: usize) -> Result<Self, MemoryError> {
    if nr_cpu_limit == 0 {
        return Err(MemoryError::ViolateConstraint);
    }

    let addr = frame.start_addr().try_to_virt().unwrap();

    let template_size = percpu_template_size();
    let dynamic_start = template_size.next_multiple_of(CACHELINE_SIZE);

    // 根据实际分配大小计算每个 CPU unit 的大小
    let unit_size = (frame.order().to_size() / nr_cpu_limit) & !(CACHELINE_SIZE - 1);

    let start = &raw const __percpu_start;

    for i in 0..nr_cpu_limit {
        let dest = unsafe { addr.as_mut_ptr::<u8>().add(i * unit_size) };

        // SAFETY: 每个目标 unit 位于独立且已分配的 backing 中，模板范围由链接器保证有效
        unsafe { start.copy_to_nonoverlapping(dest, template_size) };
    }

    let first_chunk = PerCpuChunk::try_new(Pages::Linear(frame), dynamic_start, unit_size)?;
    Ok(Self {
        first_chunk,
        count: nr_cpu_limit,
        unit_size,
    })
}
```

分配和释放也是使用最简单的

```rust
/// 分配动态 per-CPU 内存
pub fn allocate(&self, layout: Layout) -> Result<PerCpuDynHandle, MemoryError> {
    self.first_chunk
        .allocate(layout)
        .and_then(PerCpuDynHandle::new)
}

/// 释放动态分配的 per-CPU 内存
///
pub fn deallocate(&self, dyn_percpu: &PerCpuDynHandle) -> Result<(), MemoryError> {
    let dynamic_position = dyn_percpu.dynamic_position()?;

    self.first_chunk.deallocate(dynamic_position)
}
```

# 初始化

分配一段内存，然后交给 `PerCpuArea` 初始化，再为每个 per-CPU 副本写入 `delta`

```rust
fn allocate_backing(nr_cpus: usize, dynamic_start: usize) -> Result<UniqueFrames, MemoryError> {
    let target_unit_size = (dynamic_start + DYNAMIC_TARGET_SIZE).next_power_of_two();
    let target_size = target_unit_size * nr_cpus;
    let target_order = FrameOrder::from_size(target_size);

    let frame_manager = frame_manager();
    frame_manager
        .allocate(ZoneType::LinearMem, target_order)
        .or_else(|| frame_manager.allocate(ZoneType::MEM32, target_order))
        .ok_or(MemoryError::OutOfMemory)
}

pub(crate) fn try_percpu_init(nr_cpus: usize) -> Result<(), MemoryError> {
    if nr_cpus == 0 || nr_cpus > NR_CPUS_MAX {
        return Err(MemoryError::ViolateConstraint);
    }

    let frame = allocate_backing(
        nr_cpus,
        percpu_template_size().next_multiple_of(CACHELINE_SIZE),
    )?;
    let area = PercpuArea::try_new(frame, nr_cpus)?;

    PERCPU_STATE.store(PERCPU_INITIALIZING, Ordering::Relaxed);

    for cpu_id in 0..nr_cpus {
        let delta = ArchCpuLocal::delta_for(area.index(cpu_id));

        // SAFETY: delta 由该 CPU unit 的有效起点计算，目标是模板复制后的 CPU_DELTA 实例
        let cpu_delta = unsafe { ArchCpuLocal::get_ptr_for(&CPU_DELTA, delta) as *mut usize };
        // SAFETY: 每个 CPU unit 的 CPU_DELTA 仅在启动阶段写入一次
        unsafe { cpu_delta.write(delta) };

        PERCPU_DELTAS[cpu_id].store(delta, Ordering::Relaxed);
    }

    unsafe {
        // SAFETY: area 在 Ready 发布前一次性写入，之后只通过不可变引用访问。
        PERCPU_AREA.get().write(core::mem::MaybeUninit::new(area));
        // SAFETY: CPU0 unit 已复制模板并写入 CPU_DELTA，可以安全作为 BSP 的 GS 基准。
        ArchCpuLocal::activate((*PERCPU_AREA.get()).assume_init_ref().index(0));
    }

    PERCPU_STATE.store(PERCPU_READY, Ordering::Relaxed);
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn percpu_init(nr_cpus: usize) {
    try_percpu_init(nr_cpus).expect("per-CPU 初始化失败")
}
```

# PerCpuDyn

对于静态的变量，模板地址在初始化时已经由编译器/链接器分配好了可以直接取模板地址，但是动态分配不存在模板，所以需要单独的 `PerCpuDyn` 类型

```rust
pub struct PerCpuDyn<T: PerCpuInit> {
    handle: PerCpuDynHandle,
    _marker: PhantomData<T>,
}

pub(super) struct PerCpuDynHandle {
    handle: usize,
}
```

## 分配

分配时可以直接通过 `PerCpuDyn::try_new_with` 初始化，通过传入一个闭包在分配时就安全的完成所有实例的初始化

```rust 
pub fn try_new_with(mut init: impl FnMut(usize) -> T) -> Result<Self, MemoryError> {
    let area = percpu_area()?;
    let layout = Layout::new::<T>();
    let handle = area.allocate(layout)?;
    let percpu = Self::new_in(handle);

    for cpu_id in 0..area.count() {
        let delta = percpu_delta(area, cpu_id)?;
        // SAFETY: handle 由 area 分配，delta 指向已发布的 CPU unit，且每个实例只在此处初始化一次。
        let ptr = unsafe { ArchCpuLocal::get_ptr_dyn_for(&percpu, delta) as *mut T };
        unsafe {
            // SAFETY: 上述指针指向该 CPU 的独立、对齐且尚未初始化的 T 存储。
            ptr.write(init(cpu_id));
        }
    }

    Ok(percpu)
}
```

## 释放

另外，对于静态的 per-CPU 变量，它的生命周期是 `'static` 的，但是动态的 per-CPU 变量是需要释放的。另外多个 per-CPU 实例的生命周期也可能会有一定的不同，为了方便管理，统一依赖 `PerCpuDyn` 的生命周期，所以释放时需要确保所有的实例已经不再使用了再释放，这也可以利用上 Rust 的机制

per-CPU 的释放使用 `Drop` trait 自动释放

```rust
impl<T: PerCpuInit> Drop for PerCpuDyn<T> {
    fn drop(&mut self) {
        let area = percpu_area().expect("动态 per-CPU 对象析构时区域未初始化");

        for cpu_id in 0..area.count() {
            let delta = percpu_delta(area, cpu_id).expect("动态 per-CPU 对象析构时 CPU unit 无效");
            // SAFETY: handle 在当前析构前仍有效，且调用方必须保证不存在并发的远程访问。
            let ptr = unsafe { ArchCpuLocal::get_ptr_dyn_for(self, delta) as *mut T };
            unsafe {
                // SAFETY: 每个 CPU 实例恰好在构造时初始化一次，且此处恰好析构一次。
                ptr.drop_in_place();
            }
        }

        area.deallocate(&self.handle)
            .expect("动态 per-CPU 对象析构时 bitmap 状态损坏");
    }
}
```

