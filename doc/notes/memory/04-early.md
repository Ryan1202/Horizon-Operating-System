# 关于语言

涉及了内存管理的部分我使用了 Rust 来编写，主要是由于 Rust 比起 C 有更强大的类型系统，还有 0 开销的 trait 机制，至于内存安全方面，由于涉及内存管理，必定充斥了大量的 unsafe ，其实并没有多大区别。但在整个 OS 中，其实我还是倾向于 Rust 和 C 混合编写的，大概就是用 C 来搭骨架、当“胶水”，以及一些跟硬件打交道多的地方如驱动，抽象一点的东西如内存管理、线程管理、文件系统等由 Rust 来实现

# 定义

这里要先说下，为了方便区分，我将物理页称为 `Frame` , 虚拟页是 `Page`

# 早期内存分配器

早期的内存分配基本上就只是分配页表，而且页不会释放，所以只需要维护一个指针就可以了，这就是前面提到过的 `PREALLOCATED_END_PHY` （再提一嘴，这个 "PHY" 表示的是它保存的值是物理地址，自身使用的虚拟地址）

所以分配器的实现代码非常简单：

## PREALLOCATED_END_PHY

首先定义了全局变量 `PREALLOCATED_END_PHY` :

```rust
#[unsafe(no_mangle)]
pub static PREALLOCATED_END_PHY: SyncUnsafeCell<PhysAddr> = SyncUnsafeCell::new(PhysAddr::new(0));
```

这里使用 `#[unsafe(no_mangle)]` 标记提示 Rust 编译器不要 ”修饰“ 变量名，使其可以通过原名直接被 C 代码使用

`pub static` 表示定义一个（Rust 内）全局可见的静态变量；

`SyncUnsafeCell` 是标准库中的一个类型，因为 Rust 不建议使用全局可变变量了，所以使用 `SyncUnsafeCell` 包裹起来，使用的时候通过 `get` 方法获取其指针手动解引用来读取或者写入，相当于是要使用就必须使用 unsafe，强迫程序员去思考这是不是安全的；

`PhysAddr` 是我自己创建的类型，确保只能通过指定的方式操作值，本质上是一个 `usize`

## page_early_init

`page_early_init` 用来在初始化早期的内核中的页管理结构，此时 `early_allocate_pages` 还不能用，因为指针还没初始化

```rust
pub extern "C" fn page_early_init(kernel_end: usize) {
    unsafe {
        // 都向后对齐到页
        *PREALLOCATED_END_PHY.get() = PhysAddr::new(kernel_end)
            .max(*PREALLOCATED_END_PHY.get())
            .page_align_up();
    }
}
```

逻辑很简单，将 `PREALLOCATED_END_PHY` 设为内核的结束地址，然后对齐到页。也就是从内核结束的位置开始往后分配

## early_allocate_pages

```rust
// 内核启动早期分配的页都是不会释放的，如页表结构等
#[unsafe(no_mangle)]
pub extern "C" fn early_allocate_pages(count: u8) -> usize {
    unsafe {
        let addr = *PREALLOCATED_END_PHY.get();
        *PREALLOCATED_END_PHY.get() += (count as usize) * ArchPageTable::PAGE_SIZE;

        addr.as_usize()
    }
}
```

这同样是一个导出给 C 使用的函数，对应的 C 语言声明是 `size_t early_allocate_pages(uint8_t count);` 前面在 `setup_page` 里用来分配新的页表。逻辑也很简单，每次分配时将指针增加 `PAGE_SIZE` ，对应到 x86 中就是 4KB。

另外，由于这个函数只会在内核早期被使用，处于单核单线程环境下，没有竞争，所以是安全的

