# 如何描述虚拟页

在我的设计中，内核用到的虚拟内存有三种：

- 线性映射区，这里的内存是预先映射好的，通过简单的加减运算就可以转换地址
- 临时映射区，需要专门的描述结构来记录映射关系
- 页表区，预留好了虚拟内存地址，除了线性映射区之外的页都需要动态映射修改页表

实际上需要描述的只有前两种，所以我用一个枚举类型来同一表示：

```rust
pub enum Pages<'a> {
    Linear(ManuallyDrop<UniqueFrames>),
    Dynamic(&'a mut DynPages),
}
```

需要注意的是，`Pages` 并不能自动释放，而是需要手动调用释放函数
在线性映射区的页由于都是一一对应的关系，直接使用 `UniqueFrames` 即可，`DynPages` 则是我专门创建用来描述动态创建的映射的结构。

```rust
pub struct DynPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    pub(super) frame_count: usize,
    pub(super) head_frame: Option<UniqueFrames>,
}
```

动态映射的页，虚拟地址都通过红黑树来管理，以 `VmRange` 为键，`VmRange` 存的其实就是起始和结束的页号，不过为红黑树专门实现了一些增强的功能。`frame_count` 很简单，就是一共映射了多少个物理页。`head_frame` 则是指向头一组页，因为虚拟页并没有对齐到 order 的要求，所以常常会由多组 Frame 组成。

## 虚拟页号

和物理页一样，虚拟页也用了一个专门的类型用于页号，但是不同的是用了 `NonZeroUsize`，虚拟页号不允许为 0，因为这个页被设置为不存在，使访问空指针的问题能直接暴露出来

```rust
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct PageNumber(NonZeroUsize);
```



# 虚拟页管理

对于需要动态分配的虚拟页（内核），最开始都是以一整个 `VmRange` 的形式挂在红黑树上的：

```rust
static FREE_VMAP_TREE: Spinlock<RbTree> = Spinlock::new(LinkedRbTreeBase::empty());
```

同时还定义了 `Vmap` ，这记录了虚拟地址的分配信息，同时还有一个缓冲池：

```rust
const MAX_VMAP_POOL_PAGES: usize = 256;

pub(super) struct VmapPool {
    pub(super) list_head: Spinlock<ListHead<RbNode>>,
}

pub struct Vmap {
    pub(super) pools: [VmapPool; MAX_VMAP_POOL_PAGES],

    pub(super) allocated: Spinlock<RbTree>,
}
```

`256` 这个数字起始没有什么特别的讲究，只是单纯的学 Linux

关于 `RbTree` 和 `RbNode` 这两个类型，因为我自己实现的红黑树用泛型搞得比较复杂，所以在本地定义了一个别名：

```rust
type RbTree = LinkedRbTreeBase<VmRange, (), usize>;
type RbNode = LinkedRbNodeBase<VmRange, usize>;
```

从名字 `Linked` 可以看出来，比起普通的红黑树加了一个链表连接起来，其中 `usize` 用来保存一个增强参数，记录了当前这个节点及子节点可以分配的最大大小，会在红黑树更新时跟着一起更新

## 初始化

在启动时需要先对红黑树进行初始化，然后创建覆盖整个临时分配区的 `DynPages` 

```rust
pub fn init(&mut self) {
    unsafe {
        FREE_VMAP_TREE.init_with(|rbtree| {
            rbtree.init();

            let mut pages =
                kmalloc::<DynPages>(NonZeroUsize::new_unchecked(size_of::<DynPages>()))
                    .expect("Allocate slub memory failed in VmapNode::init()!");

            pages.write(DynPages::kernel());
            rbtree.insert(&mut pages.as_mut().rb_node);
        })
    };

    unsafe {
        for pool in self.pools.iter_mut() {
            pool.list_head
                .init_with(|list_head| Pin::new_unchecked(list_head).init());
        }
        self.allocated.init_with(|rbtree| rbtree.init());
    }
}
```

然后包装起来给 C 调用

```rust
#[unsafe(no_mangle)]
pub extern "C" fn vmap_init() {
    get_vmap().init();
}
```

`kmalloc` 是内核通用的内存分配器，后面再讲；`DynPages::kernel` 返回的是临时映射区的虚拟页范围

# 分配

分配时优先尝试从池中获取所需大小的页，找不到再从树中分配。分配过来的节点也需要记录到专门的树中

```rust
pub fn allocate(&mut self, count: NonZeroUsize) -> Result<NonNull<DynPages>, MemoryError> {
    // 先从快速池获取
    let mut pages = self
        .pool_get(count)
        .or_else(|| self.allocate_from_tree(count))
        .ok_or(MemoryError::OutOfMemory)?;

    // 加入已分配树
    self.allocated
        .lock()
        .insert(&mut unsafe { pages.as_mut() }.rb_node);
    Ok(pages)
}
```

## 从池中分配

逻辑不算复杂，从对应大小的链表中取出一个返回

```rust
fn pool_get(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
    let index = count.get() - 1;
    if index >= MAX_VMAP_POOL_PAGES {
        return None;
    }

    let pool = unsafe { self.pools.get_unchecked_mut(index) };
    if pool.list_head.get_relaxed().is_empty() {
        return None;
    }

    let mut list_head = pool.list_head.lock();

    let mut rb_node = list_head
        .iter(RbTree::linked_offset())
        .next()
        .expect("List is empty after checked!");

    // 通过 linked_node -> rbnode -> pages 的层级关系获取 pages
    let pages = container_of!(rb_node, DynPages, rb_node);
    
    unsafe { rb_node.as_mut().augment.get_list() }.del(&mut list_head);
    Some(pages)
}
```



## 从树中分配

`FREE_VMAP_TREE` 这颗树以 `VmRange` 为键，而 `VmRange` 还有一个要求是不能重叠，所以在红黑树中实际上是直接拿 `VmRange` 的起始页来排序的。

在搜索红黑树的时候，需要从根开始搜索，因为根的增强参数一定覆盖了整个树所有节点。然后需要决定向左搜索还是向右搜索，这里选择了和 Linux 一样，优先左子树。

```rust
/// 从红黑树中查找并分配满足条件的虚拟页块
/// 查找策略：优先左子树（smaller but sufficient），精确匹配或分割
fn allocate_from_tree(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
    let mut tree = FREE_VMAP_TREE.lock();
    let mut node = tree.root?;

    // 根节点不满足要求，整棵树都不够大
    if linked_augment!(unsafe { node.as_ref() }) < count.get() {
        return None;
    }

    // 在红黑树中查找最合适的节点（优先左子树的小块）
    loop {
        let node_ref = unsafe { node.as_mut() };

        // 如果左子树存在且最大值满足需求，优先搜索左子树
        if let Some(left) = node_ref.left
            && linked_augment!(unsafe { left.as_ref() }) >= count.get()
        {
            node = left;
            continue;
        }

        // 当前节点满足需求
        let node_count = node_ref.get_key().get_count();
        if node_count >= count.get() {
            let mut pages = container_of!(node, DynPages, rb_node);

            return if node_count > count.get() {
                // 需要分割：从 pages 中切出 count 个页，剩余部分重新插入树
                unsafe { pages.as_mut().split(count) }
            } else {
                node_ref.delete(tree.deref_mut());
                Some(pages)
            };
        }

        // 当前节点不够大，搜索右子树
        node = node_ref
            .right
            .expect("Augmented RB-tree invariant violated: child max < parent size");
    }
}
```

`linked_augment!` 宏用来获取用链表连接的红黑树中的增强参数。整体的策略就是，只要增强参数描述的大小足够，就不断向下搜索，优先左子树，然后是本节点，再就是右节点。

如果大小刚好相等，就将其从红黑树中删除并返回，否则要从其中切出合适的大小返回

## 分割

`split` 用来从一组虚拟页中切下需要的大小返回。这里为了减少红黑树的操作，通过直接修改起始页号的方式，避免删除再插入红黑树。

```rust
/// 从当前 VirtPages 中切出 count 个页
/// 修改当前节点范围为 [start+count, end]，创建新节点 [start, start+count-1] 并返回
pub(super) unsafe fn split(&mut self, count: NonZeroUsize) -> Option<NonNull<DynPages>> {
    let range = self.rb_node.get_key();
    let old_start = range.start;

    // 计算分割点：[old_start, split_point-1] 用于分配，[split_point, old_end] 放回 pool
    let split_point = old_start + count.get();

    // 修改当前节点范围
    unsafe {
        self.rb_node.get_key_mut().start = split_point;
    }

    // 分配新节点存储分配部分
    let allocated =
        kmalloc::<DynPages>(unsafe { NonZeroUsize::new_unchecked(size_of::<DynPages>()) })?;

    unsafe {
        allocated.write(DynPages::new(VmRange {
            start: old_start,
            end: split_point - 1,
        }));
    }

    Some(allocated)
}
```

# 释放

释放时先从已分配的树中删掉，如果大于池的最大大小就直接加回空闲树里，否则就放进池里

```rust
pub fn deallocate(&mut self, pages: &mut DynPages) -> Result<(), MemoryError> {
    self.allocated
        .lock()
        .delete_node(NonNull::from(&pages.rb_node));

    let node = &mut pages.rb_node;

    if node.get_key().get_count() >= MAX_VMAP_POOL_PAGES {
        FREE_VMAP_TREE.lock().insert(node);
    } else {
        self.pool_put(pages);
    }

    Ok(())
}
```

## 释放回池

直接插入对应的链表中

```rust
fn pool_put(&mut self, pages: &mut DynPages) {
    let count = pages.rb_node.get_key().get_count();
    if count >= MAX_VMAP_POOL_PAGES {
        return;
    }
    let pool = unsafe { self.pools.get_unchecked_mut(count) };

    let mut list_head = pool.list_head.lock();
    let mut list_head = unsafe { Pin::new_unchecked(&mut *list_head) };
    let node = pages.rb_node.augment.get_list();

    list_head.add_tail(node);
}
```

## 释放回树

没什么好说的，就是直接插入红黑树

# 分配选项

虚拟页也有一套 `PageAllocOptions`，这个会比 `FrameAllocOptions` 更常用，因为很少会有分配了物理内存却不访问的情况（因为要访问就必须建立映射，那么就需要通过 `PageAllocOptions` ）

## 选项

定义如下：

```rust
#[derive(Debug, Clone, Copy)]
pub struct PageAllocOptions {
    frame: FrameAllocOptions,
    contiguous: bool,
    cache_type: PageCacheType,
    zeroed: bool,
    retry: RetryPolicy,

    count: Option<NonZeroUsize>,
}
```

用来构造的函数就不放了，`FrameAllocOptions` 也不用解释，讲讲其他的：

- `contiguous` : 标识需要使用连续的物理页，一般 DMA 会用到

- `cache_type` : 缓存类型，按照英特尔白皮书的定义，有以下几种：

  - | 名称          | 效果                           |
    | ------------- | ------------------------------ |
    | WriteBack     | 写回，普通的缓存策略           |
    | WriteThrough  | 直写，写入操作不缓存           |
    | WriteCombine  | 比直写弱一些，合并写操作后写入 |
    | Uncached      | 不缓存，但是可能会重排         |
    | UncachedMinus | 完全不使用任何策略，直接写内存 |

- `zeroed` : 是否需要清零

- `retry` : 失败时的策略

- `count` : 可选的额外参数，如果设置了会严格按照 count 数分配物理页

## 预设

`PageAllocOptions` 同样有预设：

```rust
/// 预设选项：适用于内核常规分配
pub const fn kernel(order: FrameOrder) -> Self {
    Self::new(FrameAllocOptions::atomic(order)).retry(RetryPolicy::Retry(3))
}

/// 预设选项：适用于原子上下文分配
pub const fn atomic(order: FrameOrder) -> Self {
    Self::new(FrameAllocOptions::atomic(order)).retry(RetryPolicy::FastFail)
}

/// 预设选项: IO 内存分配
pub const fn mmio(start: FrameNumber, count: NonZeroUsize, cache: PageCacheType) -> Self {
    Self::new(
        FrameAllocOptions::new()
            .fallback(&[ZoneType::MEM32])
            .fixed(start, FrameOrder::new(0)),
    )
    .contiguous(false) // 由于是固定地址，走非连续分配路径可以减少对齐到order造成的浪费
    .count(count)
    .cache_type(cache)
}
```

## 最外层分配

这是分配函数的最外层包装，负责重试操作和清零

```rust
pub fn allocate<'a>(&self) -> Result<Pages<'a>, MemoryError> {
    let mut pages = self.try_alloc().or_else(|e| match self.retry {
        RetryPolicy::FastFail => Err(e),
        RetryPolicy::Retry(n) => {
            for _ in 0..n {
                if let Ok(addr) = self.try_alloc() {
                    return Ok(addr);
                }
            }
            Err(e)
        }
    });

    if let Ok(ref mut pages) = pages
        && self.zeroed
    {
        unsafe {
            pages
                .get_ptr::<u8>()
                .write_bytes(0, self.get_count().get() * ArchPageTable::PAGE_SIZE)
        };
    }

    pages
}
```

## 尝试分配

`try_alloc` 是核心分配函数，如果需要分配连续的内存则直接分配一个能满足大小需要的 order 的物理页，否则调用 `alloc_discontiguous` 处理不连续的分配

```rust
fn try_alloc<'a>(&self) -> Result<Pages<'a>, MemoryError> {
    let count = self.get_count();

    if self.contiguous {
        let (frame, zone) = if let Some(count) = self.count {
            let order = FrameOrder::new(count.get().next_power_of_two().ilog2() as u8);
            let options = self.order(order);
            options.frame.allocate()?
        } else {
            self.frame.allocate()?
        };

        if !matches!(zone, ZoneType::LinearMem) {
            let v = unsafe { get_vmap().allocate(count)?.as_mut() };

            v.map::<ArchPageTable>(frame, self.cache_type)?;

            let start = v.start_addr().to_page_number().unwrap();
            let end = start + v.frame_count - 1;
            ArchFlushTlb::flush_range(start, end);

            Ok(Pages::Dynamic(v))
        } else {
            Ok(Pages::Linear(ManuallyDrop::new(frame)))
        }
    } else {
        let v = unsafe { get_vmap().allocate(count)?.as_mut() };

        let result = self.alloc_discontiguous(v);

        if result.is_err() {
            let _ = get_vmap().deallocate(v).inspect_err(|e| {
                printk!(
                    "Failed to free virtual memory since {}, error: {:?} (memory leaked)",
                    v.start_addr(),
                    e
                )
            });

            result?;
        }

        Ok(Pages::Dynamic(v))
    }
}
```

连续内存分配成功后有两种情况：

- 如果在线性映射区，那就直接返回 `Pages::Linear` 
- 否则，再分配所需的虚拟内存，建立映射

## 分配物理不连续的内存

分配不连续的内存的方式也比较好理解，不断尝试分配小于等于剩余所需大小的物理页，逐个建立映射

```rust
fn alloc_discontiguous(&self, pages: &mut DynPages) -> Result<(), MemoryError> {
    let mut remaining = self.get_count().get();
    let mut first = None;

    let mut order = MAX_ORDER;
    let mut option = *self;
    while remaining > 0 {
        order = order.min(FrameOrder::new(remaining.ilog2() as u8));
        option = option.order(order);
        let result = option.frame.allocate();

        match result {
            Ok((mut _frames, _zone)) => {
                debug_assert!(matches!(_frames.get_tag(), FrameTag::Anonymous));

                pages.map::<ArchPageTable>(_frames, self.cache_type)?;

                if first.is_none() {
                    first = Some(());
                }

                if let FrameAllocType::Fixed { start, order } = option.frame.get_type() {
                    option.frame = option.frame.fixed(start + order.to_count().get(), order);
                }
                remaining -= order.to_count().get();
            }
            Err(_) => {
                // 当前 order 失败，尝试更小的 order
                if order.get() == 0 {
                    // order 已是最小，无法继续降低
                    break;
                }
                order = order - 1;
            }
        }
    }

    // 分配结果判断
    if remaining == 0 {
        // 完全满足需求
        let start = pages.start_addr().to_page_number().unwrap();
        let end = start + pages.frame_count - 1;
        ArchFlushTlb::flush_range(start, end);
        Ok(())
    } else if first.is_some() {
        // 部分成功但未满足需求，返回错误（避免返回残留的链表）
        pages.unlink::<ArchPageTable>()?;
        Err(MemoryError::OutOfMemory)
    } else {
        // 完全失败
        Err(MemoryError::OutOfMemory)
    }
}
```

# 接口

## 分配

通过使用 `PageAllocOptions` 可以组合出 `ioremap` 和 `vmalloc` 

```rust
pub fn ioremap<'a>(
    addr: PhysAddr,
    size: usize,
    cache_type: PageCacheType,
) -> Result<Pages<'a>, MemoryError> {
    let start = addr.to_frame_number();
    let count = frame_count(size);
    let non_zero_count = NonZeroUsize::new(count).ok_or(MemoryError::InvalidSize(size))?;

    let page_options = PageAllocOptions::mmio(start, non_zero_count, cache_type);

    page_options.allocate()
}

pub fn vmalloc<T>(
    size: NonZeroUsize,
    cache_type: PageCacheType,
) -> Result<NonNull<T>, MemoryError> {
    let count = NonZeroUsize::new(size.get().div_ceil(ArchPageTable::PAGE_SIZE))
        .ok_or(MemoryError::InvalidSize(size.get()))?;

    let order = FrameOrder::new(count.ilog2() as u8);

    let frame_options = FrameAllocOptions::new()
        .fallback(&[ZoneType::MEM32])
        .dynamic(order);

    let page_options = PageAllocOptions::new(frame_options)
        .contiguous(false)
        .cache_type(cache_type);

    page_options
        .allocate()
        .map(ManuallyDrop::new)
        .map(|mut pages| unsafe { NonNull::new_unchecked(pages.get_ptr()) })
}
```



## 释放动态映射页

```rust
pub fn vfree(vaddr: VirtAddr) -> Result<(), MemoryError> {
    let err = MemoryError::InvalidAddress(vaddr);

    let num = vaddr.to_page_number().ok_or(err.clone())?;
    let range = VmRange {
        start: num,
        end: num,
    };

    let mut node = get_vmap();
    let pages = unsafe { node.search_allocated(&range).ok_or(err)?.as_mut() };

    pages.unlink::<ArchPageTable>()?;

    node.deallocate(pages)
}
```

## 普通页分配 / 释放

```rust
pub fn kmalloc_pages<'a>(count: NonZeroUsize) -> Result<Pages<'a>, MemoryError> {
    let order = FrameOrder::new(count.get().next_power_of_two().ilog2() as u8);

    let page_options = PageAllocOptions::kernel(order);

    let pages = page_options.allocate()?;

    Ok(pages)
}

pub fn kfree_pages(vaddr: VirtAddr) -> Result<(), MemoryError> {
    let linear_start = vir_base_addr();
    let linear_end = linear_start + KLINEAR_SIZE;

    // 如果在内核线性映射区，则无需释放虚拟页
    if vaddr >= linear_start && vaddr < linear_end {
        let phy_addr = PhysAddr::new(vaddr.offset_from(linear_start));
        let frame_number = phy_addr.to_frame_number();
        let frame = Frame::get_raw(frame_number);

        let tag = Frame::get_tag_relaxed(frame_number);
        match tag {
            FrameTag::Uninited
            | FrameTag::HardwareReserved
            | FrameTag::SystemReserved
            | FrameTag::Tail
            | FrameTag::Free => {
                printk!(
                    "Trying to free unavailable frame! vaddr: {:#x}, tag: {:?}",
                    vaddr.as_usize(),
                    tag
                );
                Err(MemoryError::UnavailableFrame)
            }
            _ => unsafe {
                if UniqueFrames::try_from_raw(frame).is_none() {
                    SharedFrames::from_raw(frame, FrameOrder::new(0))
                        .expect("Not unique or shared frames!");
                }
                Ok(())
            },
        }
    } else {
        vfree(vaddr)
    }
}
```

## C 接口

这些接口都是属于 Rust 内部的，还需要导出一些接口给 C 用

```rust
#[unsafe(export_name = "kmalloc_pages")]
pub fn kmalloc_pages_c(count: usize) -> *mut core::ffi::c_void {
    let result = NonZeroUsize::new(count)
        .ok_or(MemoryError::InvalidSize(count))
        .and_then(kmalloc_pages);

    match result {
        Ok(pages) => {
            let mut pages = ManuallyDrop::new(pages);
            pages.get_ptr()
        }
        Err(e) => {
            printk!(
                "WARNING: calling kmalloc_pages_c failed: count = {:#x}, error = {:?}\n",
                count,
                e
            );
            core::ptr::null_mut()
        }
    }
}

#[unsafe(export_name = "kfree_pages")]
pub fn kfree_pages_c(ptr: usize) -> i32 {
    match kfree_pages(VirtAddr::new(ptr)) {
        Ok(_) => 0,
        Err(e) => {
            printk!(
                "WARNING: calling kfree_pages from C failed: vaddr = {:#x}, error = {:?}\n",
                ptr,
                e
            );
            -1
        }
    }
}

#[unsafe(export_name = "ioremap")]
pub extern "C" fn ioremap_c(
    addr: usize,
    size: usize,
    cache_type: PageCacheType,
) -> *mut core::ffi::c_void {
    match ioremap(PhysAddr::new(addr), size, cache_type) {
        Ok(mut ptr) => ptr.get_ptr(),
        Err(e) => {
            printk!(
                "WARNING: calling ioremap from C failed: addr = {:#x}, size = {:#x}, error = {:?}\n",
                addr,
                size,
                e
            );
            null_mut()
        }
    }
}

#[unsafe(export_name = "iounmap")]
pub extern "C" fn iounmap_c(vstart: usize) -> i32 {
    match vfree(VirtAddr::new(vstart)) {
        Ok(_) => 0,
        Err(e) => {
            printk!(
                "WARNING: calling iounmap from C failed: vstart: {:#x}, error = {:?}\n",
                vstart,
                e
            );
            -1
        }
    }
}

#[unsafe(export_name = "vmalloc")]
pub extern "C" fn vmalloc_c(size: usize, cache_type: PageCacheType) -> *mut c_void {
    let size = match NonZeroUsize::new(size) {
        Some(size) => size,
        None => {
            return null_mut();
        }
    };

    match vmalloc::<c_void>(size, cache_type) {
        Ok(ptr) => ptr.as_ptr(),
        Err(e) => {
            printk!(
                "WARNING: calling vmalloc from C failed: size = {:#x}, error = {:?}\n",
                size,
                e
            );
            null_mut()
        }
    }
}
```

由于 `MemoryError` 没办法传递给 C，所以在出错时直接打印错误，然后返回一个小于 0 数表示出错了

