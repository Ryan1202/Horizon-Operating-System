# 背景

## 避免多核间同步

per-CPU 变量是在多核系统中很重要的一个优化手段，将变量放在（大部分情况下）仅限于当前 CPU 访问的区域，避免了不同核心缓存之间的同步需求。其他的手段如自旋锁、原子操作都需要通过如 MESI 协议来在不同核心之间同步，更糟糕的情况可能需要先使另一个核心的缓存行失效然后重新读取

在 Linux 的 Buddy、Slab 等内存分配器中都会使用 per-CPU 变量在本地核心建立一个缓冲池，对于高频分配、释放的内存都可以在本地无锁化实现

## 硬件支持

per-CPU 变量能高效地实现的一个重要条件是在写代码的时候不需要特地区分当前使用哪个 CPU 的缓冲区（如 `pointers[cpu_id]` 就需要额外经过一次索引），在 x86 中，段寄存器就是一个绝佳的选择

段寄存器最早在 x86 的 16 位模式里就出现了，当时是为了弥补以前 16 位寻址空间的不足，加入段寄存器使用 `segment : offset` 的寻址模式来在保留兼容性的同时扩展寻址空间到 20 位（1MB）

在 386 时代，推出了 32 位保护模式，此时段寄存器简单的和 `offset` 移位相加来获取地址了，在新的分段内存管理模式中段寄存器的值被作为索引使用（被称为段选择子）。全局描述符表 (Global Descriptor Table, GDT) 和 局部描述符表（Local Descriptor Table, LDT）定义了每个段选择子对应的寻址范围（通过基地址+长度限制来定义），不同的段能定义访问权限，访问没有权限的段会触发 CPU 异常并交给操作系统处理，这也是 “保护模式” 中 “保护” 的含义。

> 在 32 位保护模式中更常用的其实是分页内存管理，操作系统以页为单位管理权限，同时能够使用虚拟地址空间，CPU 使用的地址无需和物理地址一对一，而是可以以页为单位重新排布，更加灵活。
>
> 需要注意的是，在 32 位保护模式下段寄存器仍然是 16 位的，可能是不再直接用于地址计算所以当时设计者认为无需拓展位宽。另外，虽然段寄存器被用于索引了，但也不是 `0, 1, 2, ...` 这样的索引，而是以 `8` 为步进的，因为一个描述符占 `8` 字节，CPU 会直接使用段寄存器中的只与保存在 `GDTR/LDTR` 中的描述符表基地址相加得到段描述符的地址来读取。

有了段寄存器就很方便了，只要为每个 CPU 核心中的段寄存器设置不同的地址，然后访问的时候拿着相同的偏移，通过 `segment:[offset]` 就可以使用不同核心各自的副本了。不知道是巧合还是有意的设计，在保护模式下 `fs`, `gs` 两个段寄存器被空出来了可以用于 per-CPU 变量，内核常常选择 `gs` 寄存器，而用户态常常使用 `fs` 作为线程本地存储（Thread Local Storage, TLS）

到了 64 位时代，`fs` 和 `gs` 寄存器也拓展到了 64 位宽度，但是默认还是只使用低 32 位，需要通过写入 MSR 寄存器 `IA32_FS_BASE/IA32_GS_BASE` 才能将 64 位值写入其中，后来还出现了 `swapgs`, `rdfsbase`, `rdgsbase`, `wrfsbase`, `wrgsbase` 等指令来方便地操作完整的 64 位。由于在 64 位模式下分段内存管理也不再使用，所以 `fs` 和 `gs` 又恢复到了作为基地址直接参与地址计算

# 设计

目前阶段，先只设计静态的 per-CPU 变量，这样比较简单

## 链接

> 注意这里的 “段” 和前文的 “段” 不是同一个概念

对于 per-CPU 变量，我们需要将它放到专门的 `.data..percpu` 段里（严格来说是节 `section`），对应的链接脚本设计如下：
```lds
    .data..percpu ALIGN(PAGE_SIZE) : AT(ADDR(.data..percpu) - VIR_BASE)
    {
        __percpu_start = .;

        . = ALIGN(PAGE_SIZE);
        KEEP(*(.data..percpu..page_aligned))
        KEEP(*(.data..percpu..page_aligned.*))

        . = ALIGN(CACHELINE_SIZE);
        KEEP(*(.data..percpu))
        KEEP(*(.data..percpu.*))

        __percpu_end = .;
    } : data
```

首先整个 `.data..percpu`  需要放到对齐到页大小的内存地址中，方便直接按页分配内存；接着定义了 `__percpu_start` 和 `__percpu_end` 两个符号，用于指示开始和结束的内存地址；另外还定义了 `.data..percpu..page_aligned`，目前没啥用；剩余的正常的 per-CPU 变量只需要起始地址对齐到缓存行大小就行

## 使用方式

首先需要明确 per-CPU 的使用方式应该是类似汇编 `gs:[offset]` 的形式，所以每个 CPU 的副本的地址计算方式如下：
```
变量地址 - percpu段起始地址 + percpu副本起始地址
```

其中后面两部分是可以在分配完内存之后就直接确定的，所以可以直接设置

```
gs = delta = percpu副本起始地址 - percpu段起始地址
```

当然，两个地址谁数值上更大是不确定的（虽然实际上是确定的），不过得益于 CPU 计算时的溢出机制，直接拿计算完的结果作为基地址来和偏移相加也可以得到正确的结果

为什么要这么设计呢？因为在编译和链接时，编译器/链接器给全局变量分配的地址是一个实际有效的地址而不是偏移，而通过使用 `delta` 作为基地址的方式又可以省掉这额外的一次计算，在程序中变量就可以保持实际指向 `.data..percpu` 中的模板而不用特地修改

# 实现

我实际上是使用 Rust 来实现 per-CPU 的，和 Linux 的使用方式会比较不一样

## 定义变量

参考 Rust 的 `thread_local!` 宏，我设计了一个 `cpu_local!` 宏

```rust
#[macro_export]
macro_rules! cpu_local {
    () => {};
    (
        $(#[$attr:meta])*
        $vis:vis static $name:ident: $ty:ty = $value:expr;
        $($rest:tt)*
    ) => {
        $(#[$attr])*
        #[used]
        #[unsafe(link_section = ".data..percpu")]
        $vis static $name: $crate::kernel::memory::percpu::PerCpu<$ty> = $crate::kernel::memory::percpu::PerCpu::new($value);
        $crate::cpu_local!($($rest)*);
    };
}
```

仅仅是给全局变量定义增加一个将其放入 `.data..percpu` 的标记和防止编译器将其自动优化没掉的 `#[used]` 标记，另外就是用专门的 `PerCpu<T>` 类型来包裹防止直接当正常变量使用，没有什么特殊的操作。实际使用时的体验也仅仅是在普通的全局变量生命外面套一层宏：

```rust
cpu_local!(
    pub static PERCPU_DELTA: usize = 0;
);
```

将宏展开会变成这样：

```rust
#[used]
#[unsafe(link_section = ".data..percpu")]
pub static PERCPU_DELTA: PerCpu<usize> = PerCpu::new(0);
```

## PerCpu类型

对于 per-CPU 变量使用了专门的 `PerCpu<T>` 类型

```rust
pub unsafe trait PerCpuInit: Sized + 'static {}

#[repr(transparent)]
pub struct PerCpu<T: PerCpuInit> {
    value: T,
}

unsafe impl<T: PerCpuInit> Sync for PerCpu<T> {}

impl<T: PerCpuInit> PerCpu<T> {
    pub const fn new(value: T) -> Self {
        Self { value }
    }

    /// 获取 per-CPU 模板的指针，不可以直接访问该指针
    pub const fn get_template(&self) -> *const T {
        &raw const self.value
    }

    pub fn get_local<'a>(&self, preempt: &'a PreemptGuard) -> CpuLocalGuard<'a, T> {
        CpuLocalGuard::new(preempt, self)
    }
}
```

如果需要将一个类型作为 per-CPU 变量，需要其显示声明支持（即实现 `PerCpuInit` trait），这一 trait 已经自动为 Rust 中的基础类型和对应的定长数组实现了。另外 per-CPU 变量由这一机制本身确保能安全的在线程间共享（因为实际上除了多核不可能有多个线程同时持有引用）

## 跨架构抽象

### per-CPU 机制本身需要的操作

```rust
pub trait CpuLocal {
    /// 激活当前 CPU 的 per-CPU 实例
    ///
    /// # Safety
    ///
    /// `base` 必须是一个始终有效的 per-CPU 实例的起始地址，并且该实例已经被初始化
    unsafe fn activate(base: *mut u8) -> usize;

    /// 获取 per-CPU 实例的指针
    fn get_ptr<T: PerCpuInit>(percpu: &PerCpu<T>) -> *const T {
        let delta = PERCPU_DELTA.read();
        unsafe { Self::get_ptr_for(percpu, delta) }
    }

    /// 获取某个 CPU 的 per-CPU 实例的指针
    ///
    /// # Safety
    ///
    /// `delta` 必须是一个有效的偏移量，指向某个 CPU 的 per-CPU 实例
    unsafe fn get_ptr_for<T: PerCpuInit>(percpu: &PerCpu<T>, delta: usize) -> *const T;
}
```

对于结构体这种相对复杂的变量，最好还是先获取到其真正的地址，然后再使用，避免了反复使用段寄存器（因为没法指定编译器使用这种特殊的访存模式所以只能写内联汇编并封装起来），另外 Rust 中的 bool 类型无法保证其内存大小，所以也不能直接读写

### 标量操作

即对于 Rust 中的基础变量类型进行直接的加减

```rust
pub trait PerCpuScalar<T: PerCpuInit>: PerCpuReadWrite<T> {
    fn add(&self, value: T);
    fn sub(&self, value: T);

    /// 对当前 CPU 的 per-CPU 实例进行加法操作，返回操作前的值
    fn fetch_add(&self, value: T) -> T;
    /// 对当前 CPU 的 per-CPU 实例进行减法操作，返回操作前的值
    fn fetch_sub(&self, value: T) -> T;

    fn increase(&self);
    fn decrease(&self);
}
```

### 读写

除了标量类型之外，裸指针其实也是能直接读写的，所以读写单独抽出来成为一个 trait

```rust
pub trait PerCpuReadWrite<T: PerCpuInit> {
    fn read(&self) -> T;
    fn write(&self, value: T);
}
```

## PercpuArea

描述 CPU 实际使用的内存区域，允许多个核心使用的区域合并一起分配

```rust
pub struct PercpuArea {
    frame: UniqueFrames,
    /// 该区域中包含的 CPU 核心数量
    count: usize,
}

// SAFETY: PERCPU_AREA 在启动阶段一次性初始化；其持有的 frame 在内核生命周期
// 内不会释放，后续只通过不可变引用查询各 CPU 的区域地址。
unsafe impl Sync for PercpuArea {}

impl PercpuArea {
    pub fn new(frame: UniqueFrames, nr_cpus: usize) -> Self {
        assert!(nr_cpus > 0, "per-CPU area requires at least one CPU");

        let addr = frame.start_addr().try_to_virt().unwrap();

        let template_size = percpu_template_size();
        let stride = percpu_stride();
        let total_size = stride
            .checked_mul(nr_cpus)
            .expect("per-CPU area size overflow");
        assert!(
            frame.order().to_size() >= total_size,
            "allocated frames are smaller than the per-CPU area"
        );

        let start = with_exposed_provenance::<u8>((&raw const __percpu_start).addr());

        for i in 0..nr_cpus {
            let dest = unsafe { addr.as_mut_ptr::<u8>().add(i * stride) };
            unsafe {
                start.copy_to_nonoverlapping(dest, template_size);
            };
        }

        Self {
            frame,
            count: nr_cpus,
        }
    }

    pub fn index(&self, index: usize) -> *mut u8 {
        assert!(index < self.count);

        let start = self
            .frame
            .start_addr()
            .try_to_virt()
            .unwrap()
            .as_mut_ptr::<u8>();
        unsafe { start.add(index * percpu_stride()) }
    }
}
```

`new()` 负责在已经分配好的内存中完成初始化（即将模板中的数据复制进去），`index()` 用于在该区域内部进行索引

目前由于还是单核，且未来很长一段时间应该还不会涉及需要多个的情况，所以直接定义为全局变量

```rust
pub(super) static PERCPU_AREA: SyncUnsafeCell<MaybeUninit<PercpuArea>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());
```

## 初始化

初始化很简单，计算出需要的内存大小，分配内存，然后初始化内存，再激活

```rust
pub fn percpu_init(nr_cpus: usize) {
    assert!(
        nr_cpus > 0,
        "per-CPU initialization requires at least one CPU"
    );
    let size = percpu_stride().checked_mul(nr_cpus).unwrap();
    let order = FrameOrder::from_size(size);

    let frame_manager = frame_manager();
    let frame = frame_manager
        .allocate(ZoneType::LinearMem, order)
        .or_else(|| frame_manager.allocate(ZoneType::MEM32, order))
        .unwrap();

    let area = PercpuArea::new(frame, nr_cpus);

    unsafe {
        let delta = ArchCpuLocal::activate(area.index(0));
        PERCPU_DELTA.write(delta);

        PERCPU_AREA.get().write(MaybeUninit::new(area))
    }
}
```

# CpuLocalGuard

使用 per-CPU 变量非常重要的一点是要防止在使用过程中被抢占然后调度到其他 CPU 核心去了，`CpuLocalGuard` 就用于提供这一保证，与锁相似，`CpuLocalGuard` 会在离开作用域的时候自动解除

目前采用的是最简单的方案，即直接使用 `PreemptGuard`，只要没发生抢占就不会被移动到其他核心，但是未来可能还是需要改，因为两者不是完全相同或者具有包含关系的概念

```rust
pub struct CpuLocalGuard<'a, T: Sized> {
    preempt_guard: &'a PreemptGuard,
    inner: NonNull<T>,
}

impl<'a, T: Sized> CpuLocalGuard<'a, T> {
    pub fn new(preempt_guard: &'a PreemptGuard, percpu: &PerCpu<T>) -> Self
    where
        T: PerCpuInit,
    {
        let inner = NonNull::new(ArchCpuLocal::get_ptr(percpu) as *mut T).unwrap();
        Self {
            preempt_guard,
            inner,
        }
    }

    pub fn map<F, R>(self, f: F) -> CpuLocalGuard<'a, R>
    where
        F: FnOnce(&T) -> &R,
        R: PerCpuInit,
    {
        let inner = f(unsafe { self.inner.as_ref() });
        CpuLocalGuard {
            preempt_guard: self.preempt_guard,
            inner: NonNull::from(inner),
        }
    }

    pub fn preempt_guard(&self) -> &PreemptGuard {
        self.preempt_guard
    }
}

impl<'a, T: PerCpuInit> Deref for CpuLocalGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { self.inner.as_ref() }
    }
}
```

