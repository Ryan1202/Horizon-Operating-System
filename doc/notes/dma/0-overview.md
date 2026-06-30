# 类型

DMA 根据分配的方式区别有几个不同系列的API

- Coherent：用于分配一组满足设备要求的物理连续的页，可以直接被内核或设备访问
- Pool：基于 Slub，通过对象缓存来分配设备可访问的小段内存
- Stream：流式缓冲区，直接将传入的缓冲区用于 DMA 传输
- Scatter-Gather：将一组已有的不连续的内存串起来，一些设备支持直接通过类似的描述方式访问不连续的缓冲区

另外还有一个隐藏在内部的 `SoftwareIotlb` ，用软件模拟一个 IOTLB ，在低地址创建一个缓冲区给设备使用，然后通过同步 API 和原始的缓冲区同步。

# 结构

内核 DMA 框架的底层实现被分成了三个基础部分：`Source` , `Mapping` , `Sync` 。这么拆分是希望保留扩展能力，未来能根据需要重新组合这些基础能力。

## Source

`Source` 层负责提供设备可用的内存缓冲区。其中分配的返回中物理地址是必须提供的，虚拟地址则是可选的，因为设备（或者 IOMMU）需要访问内存只能通过物理地址来寻找。

这里将不同的分配方式都收束到了一个接口来解耦，但同时也允许跳过这个接口直接使用

```rust
pub enum Source {
    Coherent { size: usize },
    Pool { pool: NonNull<Pool> },
    Direct { paddr: PhysAddr, size: usize },
    SoftwareIotlb { paddr: PhysAddr, size: usize },
}
```

`Coherent` 方式是直接分配页，所以只需要大小作为参数；`Pool` 的大小是固定的，但是需要提供对象缓存池；`Direct` 则是直接使用原始缓冲区，需要提供物理地址和大小；`SoftwareIotlb` 则是通过系统预留的区域分配一个设备可用的缓冲区，但是要注意手动同步缓冲区。

### Coherent

最核心的部分就是直接调用页分配接口，根据设备的寻址范围设定 Buddy 使用的 Zone，还有根据一致性要求设置 CPU 的页缓存方式。

这里的实现方式是先通过单独的 `new` 为一个使用 DMA 的设备创建一个符合需要的页分配器，在分配时直接从分配器分配

#### 创建

根据设备限制创建分配器，首先是决定物理页的 Zone

```rust
let mask = PhysAddr::new(constraints.coherent_mask);
let frame_options = if mask > ZoneType::MEM32.range().end {
    FrameAllocOptions::new().fallback(&[ZoneType::LinearMem, ZoneType::MEM32])
} else {
    FrameAllocOptions::new().fallback(&[ZoneType::MEM32])
};
```

这里 `range()` 返回使用的是标准库的 `core::ops::Range<Idx>` 类型，可以直接通过 `A..B` 的语法创建。为了方便比较也直接把 `mask` 转成 `PhysAddr` 类型和 32 位寻址边界作比较，如果超过了则优先尝试 `LinearMem` 区域，否则只能尝试 `MEM32` 。

```rust
let cache_type = if coherency {
    PageCacheType::WriteBack
} else {
    PageCacheType::Uncached
};
```

然后决定使用的缓存类型，这里类似 Linux 只用了一个 `coherency` 的布尔值决定，虽然内核 / x86 CPU 支持更多类型，但暂时还是先只用 `WriteBack` 和 `Uncached` 两种类型。

最后创建出页分配器

```rust
CoherentAllocator {
    options: PageAllocOptions::new(frame_options)
        .cache_type(cache_type)
        .contiguous(true)
        .zeroed(true),
}
```

#### 分配

这里不完全是直接调用内核的页分配来分配到页就完事了，因为 Buddy 分配器为了效率只是根据 Zone 进行粗暴的划分，还需要根据设备更细的寻址范围和对齐要求进行检查，如果不满足就只能范围错误，没有更好的解决方案

```rust
let mut page = self.options.order(order).allocate()?;

let vaddr = page.start_addr();
let paddr = page
    .get_first_frame()
    .map(|f| f.start_addr())
    .expect("Allocation successed but first frame not exists");
```

先从分配结果获取到虚拟和物理地址，这里 `get_first_frame()` 在成功分配时不应该为 `None` ，所以直接 panic

```rust
let alloc_size = order.to_count().get() * ArchPageTable::PAGE_SIZE;
if constraints.crosses_boundary(paddr, alloc_size) {
    if let Err(e) = kfree(page.get_ptr::<()>()) {
        printk!(
            "WARN: Failed to free coherent page at {:p}: {:?}",
            page.start_addr(),
            e
        );
    }
    return Err(MemoryError::InvalidVirtualAddress(vaddr));
}
```

然后检查是否跨越边界，比如 ISA DMA 就有要求不能跨越 64 K 边界。`get_ptr::<()>()` 使用范型表示指针类型，这里的 `()` 类型类似 C 的 `void`

```rust
if !constraints.is_coherent_satisfied(paddr) {
    if let Err(e) = kfree(page.get_ptr::<()>()) {
        printk!(
            "WARN: Failed to free coherent page at {:p}: {:?}",
            page.start_addr(),
            e
        );
    }

    return Err(MemoryError::InvalidVirtualAddress(page.start_addr()))
}

Ok((vaddr, paddr))
```

最后检查其余限制

#### 释放

直接 `kfree`

### Pool

`Pool` 目前就是 `MemCache` 的一个包装

```rust
#[repr(transparent)]
pub struct Pool {
    cache: MemCache,
}
```

#### 创建

直接使用和 `Cohernent` 相同的页分配器参数创建一个对象缓存池

```rust
let config = CacheConfig::new(name, object_size).ok()?;
let config = if align > 0 {
    config.align(align)
} else {
    config
};
```

先创建了对象缓存的配置，指定了对齐参数

中间创建分配器，略

```rust
let cache = MemCache::new(config, page_opts)?;

Some(cache.cast())
```

然后分配一块内存初始化池，由于这里通过 `#[repr(transparent)]` 保证了 `Pool` 和 `MemCache` 的内存布局完全相同，所以直接使用 `NonNull::cast()` 转换，少了一次内存分配

#### 分配

分配页只是一个简单的包装，这里地址转换不应该失败，以防万一失败了直接 panic

```rust
let ptr = self
    .cache
    .allocate::<u8>()
    .ok_or(MemoryError::OutOfMemory)?;

let vaddr = VirtAddr::new(ptr.addr().get());
let paddr = PageTableOps::<ArchPageTable>::translate(current_root_pt(), vaddr)
    .expect("Pool allocation successed but not exist in page table");
Ok((vaddr, paddr))
```

#### 释放

释放更是简单，直接 `kfree`

```rust
pub fn deallocate(&self, ptr: NonNull<u8>) -> Result<(), MemoryError> {
    kfree(ptr)
}
```

### Direct

Direct 唯一的工作就是在转换虚拟地址为物理地址的同时检查是否物理连续

```rust
pub fn translate(vaddr_base: VirtAddr, size: usize) -> Option<PhysAddr> {
    PhysAddr::try_from_linear_virt(vaddr_base + size)
        .and(PhysAddr::try_from_linear_virt(vaddr_base))
        .or_else(|| {
            let root_pt = current_root_pt();

            let mut vaddr = vaddr_base;
            let paddr = PageTableOps::<ArchPageTable>::translate(root_pt, vaddr)?;
            let end = vaddr + size;
            while vaddr < end {
                let tmp = PageTableOps::<ArchPageTable>::translate(root_pt, vaddr)?;

                // 检查物理地址是否连续
                if tmp - paddr != vaddr - vaddr_base {
                    return None;
                }

                let step = (ArchPageTable::PAGE_SIZE - vaddr.page_offset()).min(end - vaddr);
                vaddr += step;
            }
            Some(paddr)
        })
}
```

这里做了一个简单的优化，如果头尾都在 Linear 区域内，则能直接计算得到物理地址

### SoftwareIotlb

#### 配置

基础的参数配置如下

```rust
const SLOT_SIZE: u16 = 2048;
const MAX_ALLOC_SLOTS: u16 = 128;
const TOTAL_SIZE: usize = 2 * 1024 * 1024; // 2 MiB

const TOTAL_AREAS: usize = 4;
```

由于目前没有 CMA 分配连续的大内存，所以选择了 Buddy 支持的最大大小 2 MiB 作为总缓存池大小

**BounceAddr**

引入了一个新的类型用来表示在BouncePool内的物理地址，避免重复检查

```rust
/// 该结构体用于表示 Bounce Pool 中的物理地址
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct BounceAddr(usize);

impl_addr!(BounceAddr);

impl BounceAddr {
    /// 创建一个新的 BounceAddr，确保地址在 Bounce Pool 的范围内
    ///
    /// # Safety
    ///
    /// 调用者必须确保 BouncePool 已初始化，否则行为未定义。
    pub fn new(addr: PhysAddr) -> Option<Self> {
        BouncePool::get_pool()
            .contains(addr)
            .then(|| BounceAddr(addr.as_usize()))
    }

    pub const fn as_paddr(&self) -> PhysAddr {
        PhysAddr::new(self.0)
    }
}
```

`impl_addr` 是我自己定义的一个宏，用来实现加减和比较等操作

#### Slot

分配的基本单位是 Slot ，使用 `SlotMeta` 来保存各个 Slot 的信息

```rust
#[derive(Clone)]
pub struct SlotMeta {
    /// 当前 Slot 映射的原始物理地址
    origin_addr: PhysAddr,
    /// 自当前 Slot 起始的剩余大小，单位为字节
    origin_size: u32,
    /// 当前 Slot 及其后续连续空闲 Slot 的数量，None 表示已分配
    n_slots: Option<NonZeroU16>,
    /// Slot 内部用于对齐的填充槽位数量，位于前部
    padding_slots: u16,
    /// 当前 Slot 是否为分配区域的头部
    allocation_head: bool,
}
```

其中只保存了原缓冲区的物理地址，因为原缓冲区不一定来自内核区域的内存，所以使用物理地址是最靠谱的选择

`SlotMeta` 有几个基本的工具函数：

**is_free()**

```rust
pub const fn is_free(&self) -> bool {
    self.n_slots.is_some()
}
```

用来判断当前 Slot 是不是空闲 Slot

**origin_addr()**

```rust
pub const fn origin_addr(&self) -> PhysAddr {
    self.origin_addr
}
```

用于获取被映射到该 Slot 的缓冲区物理地址

**origin_size()**

```rust
pub const fn origin_size(&self) -> usize {
    self.origin_size as usize
}
```

用于获取原缓冲区的大小

#### Area

Slot 被以 Area 为单位分开管理，这里有一个重要的假设：除去前部的 padding 外，槽位的数量应该是刚好满足 `origin_size` 需要的

```rust
pub struct IotlbArea {
    lock: Spinlock<AreaInner>,
}

pub struct AreaInner {
    /// 已分配的 Slot 数量
    used: u16,
    /// 缓存上次分配的槽位索引，优化下次分配的搜索
    index: u16,
    /// 当前 Area 的首个 Slot 在整个池中的偏移量
    slot_offset: u32,
    /// 当前 Area 的槽位元数据
    slots: &'static mut [SlotMeta],
}
```

这里使用了一个自选锁来保护每个 Area 的数据

**find_consecutive_free()**

分配的过程其实就是寻找满足 Slot 数量和对齐要求的一组连续的 Slot

```rust
/// 在当前 Area 中查找连续的空闲槽位，满足对齐要求
///
/// `n_slots`: 需要查找的连续空闲槽位数量
///
/// `align_slots`: 对齐要求，单位为槽位数
///
/// 返回值: 如果找到满足条件的连续空闲槽位，返回 `Some((padding, local_index))`，
/// `padding` 表示为了满足对齐要求需要跳过的槽位数量，
/// `local_index` 表示找到的连续空闲槽位的起始索引；
/// 如果未找到，返回 `None`
fn find_consecutive_free(&self, n_slots: u16, align_slots: usize) -> Option<(u16, u32)> {
    let len = self.slots.len();
    let start = self.index as usize;
    let mut i = start;
    loop {
        let skip = if let Some(available) = self.slots[i].n_slots {
            let available = available.get() as usize;
            let padding = i % align_slots;

            if available >= n_slots as usize + padding {
                return Some(((align_slots - offset) as u16, i as u32));
            }

            available
        } else {
            1
        };

        if i < start && i + skip >= start {
            return None;
        }
        i = (i + skip) % len;
    }
}
```

**try_alloc**

先找到连续的空槽位，然后将其标记为已分配

```rust
/// 尝试在当前 Area 中分配连续的槽位，实际的分配大小由 `n_slots` 决定，`alloc_size` 仅用于记录原始物理地址的大小
///
/// `n_slots`: 需要分配的槽位数量
///
/// `alloc_size`: 需要分配的总大小，单位为字节
///
/// `origin_addr`: 需要映射的原始物理地址
///
/// `align_slots`: 对齐要求，单位为槽位数
///
/// 返回值: 如果分配成功，返回 `Some(local_index)`，表示分配的槽位在当前 Area 中的本地索引；如果分配失败，返回 `None`
fn try_alloc(
    &mut self,
    n_slots: u16,
    alloc_size: u32,
    origin_addr: PhysAddr,
    align_slots: usize,
) -> Option<u32> {
    let (padding, local_index) = self.find_consecutive_free(n_slots, align_slots)?;

    self.mark_allocated(local_index, n_slots, alloc_size, origin_addr, padding);
    Some(local_index)
}
```

**mark_allocated**

```rust
/// 将一组 Slot 标记为已被分配
///
/// `index`: 分配的起始 Slot 索引
///
/// `n_slots`: 分配的 Slot 数量
///
/// `origin_size`: 分配的原始物理地址大小
///
/// `origin_addr`: 分配的原始物理地址
///
/// `padding_slots`: 为了满足对齐要求而保留的 Slot
fn mark_allocated(
    &mut self,
    index: u32,
    n_slots: u16,
    origin_size: u32,
    origin_addr: PhysAddr,
    padding_slots: u16,
) {
    let i = index as usize;
    let n = n_slots as usize;

    let mut origin_addr = origin_addr;
    let mut origin_size = origin_size;

    self.slots[i].mark_allocated_head(origin_addr, origin_size, padding_slots);

    for slot in &mut self.slots[i + 1..i + n] {
        origin_addr += SLOT_SIZE as usize;
        origin_size = origin_size
            .checked_sub(SLOT_SIZE as u32)
            .expect("Original size is much smaller than allocated size");

        slot.mark_allocated(origin_addr, origin_size);
    }

    for j in (0..i).rev() {
        if self.slots[j].n_slots.is_some() {
            break;
        }

        self.slots[j].n_slots = Some(NonZeroU16::new((i - j) as u16).unwrap());
    }

    self.used += n_slots as u16;
    self.index = if i + n < self.slots.len() {
        (i + n) as u16
    } else {
        0
    };
}
```

将已被分配的槽位标记为已分配，同时更新当前槽位之前的空闲槽位的计数

**expand_free_region**

这是一个比较重要的函数，用于释放时寻找前后可以合并的区域

```rust
/// 扩展连续空闲槽位的范围，向前和向后查找连续的空闲槽位
///
/// `index`: 当前槽位的索引
///
/// `n_slots`: 当前槽位的数量
///
/// 返回值: 返回一个 `Range<usize>`，表示连续空闲槽位的起始和结束索引范围
fn expand_free_region(&self, index: usize, n_slots: usize) -> Range<usize> {
    let len = self.slots.len();

    let mut start = index;
    while start > 0 && self.slots[start - 1].is_free() {
        start -= 1;
    }

    // 向后只用找一次，因为每一次释放都会合并连续的空闲槽位，所以后续的槽位已经是连续空闲的
    let mut end = index + n_slots;
    if end < len && self.slots[end].is_free() {
        end += self.slots[end]
            .n_slots
            .map(|v| v.get() as usize)
            .unwrap_or(0);
    }

    start..end
}
```

**free_slot**

```rust
/// 释放一组连续的槽位，并尝试合并相邻的空闲槽位
///
/// `local_index`: 需要释放的槽位在当前 Area 中的本地索引
fn free_slots(&mut self, local_index: u16) {
    let i = local_index as usize;
    let slot = &mut self.slots[i];

    // 这里依赖 Slot 数量刚好是 padding + ceil(origin_size / SLOT_SIZE)
    let n_slots =
        slot.padding_slots as usize + (slot.origin_size as usize).div_ceil(SLOT_SIZE as usize);

    *slot = SlotMeta {
        origin_addr: PhysAddr::new(0),
        origin_size: 0,
        n_slots: Some(Self::ONE),
        padding_slots: 0,
        allocation_head: false,
    };

    // 后面会被合并，所以先不设置 n_slots

    let region = self.expand_free_region(i, n_slots);

    for k in region.clone().rev() {
        self.slots[k].n_slots = Some(NonZeroU16::new((region.end - k) as u16).unwrap());
    }

    self.used -= n_slots as u16;
    self.index = region.start as u16; // 更新索引为释放区域的起始位置，以便下次分配时从这里开始搜索
}
```

#### BouncePool

这是直接对外暴露的类型，通过这一类型间接管理所有 Area 内的所有 Slot

为了在 no_std 环境下使用单例稍微有点麻烦：

```rust
static BOUNCE_POOL: SyncUnsafeCell<MaybeUninit<BouncePool>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());
```

为了确保安全的使用，在内核还在单线程时需要对其完成初始化，之后就一直是 `assume_init` 而且只读，只有通过 `IotlbArea` 内的 `Spinlock` 才能获取到可变引用

**get_pool**

用于获取 `BouncePool` 实例

```rust
pub fn get_pool<'a>() -> &'a BouncePool {
    unsafe { (*BOUNCE_POOL.get()).assume_init_ref() }
}
```

**allocate**

分配 Slot

```rust
/// 分配一组连续的 Slot ，并返回对应的物理地址和虚拟地址
///
/// `origin_size`: 原始大小，单位为字节
///
/// `origin_addr`: 需要映射的原始物理地址
///
/// `align`: 对齐要求，单位为字节
///
/// 返回值: 如果分配成功，返回 `Some((BounceAddr, VirtAddr))`，表示分配的物理地址和虚拟地址；否则返回 `None`
pub fn allocate(
    &self,
    origin_size: u32,
    origin_addr: PhysAddr,
    align: usize,
) -> Option<(BounceAddr, VirtAddr)> {
    let n_slots = origin_size.div_ceil(SLOT_SIZE as u32) as u16;
    if n_slots > MAX_ALLOC_SLOTS {
        return None;
    }

    // 计算最小公倍数
    fn gcd(mut a: usize, mut b: usize) -> usize {
        while b != 0 {
            let t = b;
            b = a % b;
            a = t;
        }
        a
    }
    fn lcm(a: usize, b: usize) -> usize {
        a * b / gcd(a, b)
    }
    let align_slots = lcm(align, SLOT_SIZE as usize);

    for area in self.areas.iter() {
        let mut inner = area.lock.lock();

        if let Some(local_index) =
            inner.try_alloc(n_slots, origin_size, origin_addr, align_slots)
        {
            let index = inner.slot_offset as usize + local_index as usize;
            let offset = index * SLOT_SIZE as usize;

            let paddr = self.paddr_base + offset;
            let vaddr = self.vaddr_base + offset;

            return Some((paddr, vaddr));
        }
    }
    None
}
```

**deallocate**

释放 Slot

```rust
/// 释放一组连续的 Slot
///
/// `addr`: 需要释放的 Slot 的物理地址
pub fn deallocate(&self, addr: BounceAddr) {
    let paddr = addr.as_paddr();
    let index = (paddr - self.paddr_base) / SLOT_SIZE as usize;

    for area in self.areas.iter() {
        let mut area = area.lock.lock();
        let offset = area.slot_offset as usize;

        if index >= offset && index < offset + area.slots.len() {
            area.free_slots((index - offset) as u16);
            return;
        }
    }
}
```

**get_ptr**

```rust
/// 尝试获取 Slot 的指针，如果 `addr` 不在池的范围内，返回 `None`
pub fn get_ptr<T>(&self, addr: BounceAddr) -> NonNull<T> {
    let offset = addr - self.paddr_base;
    let virt = self.vaddr_base + offset;
    NonNull::new(virt.as_mut_ptr::<T>()).unwrap()
}
```

**clone_slot**

在需要访问多个元素的情况下比起占用着锁不如直接复制一份

```rust
/// 复制一份 Slot 元数据
pub fn clone_slot(&self, addr: BounceAddr) -> SlotMeta {
    let index = (addr - self.paddr_base) / SLOT_SIZE as usize;

    for area in self.areas.iter() {
        let area = area.lock.lock();
        let offset = area.slot_offset as usize;

        if index >= offset && index < offset + area.slots.len() {
            return area.slots[index - offset].clone();
        }
    }
    panic!("BouncePool: Invalid Bounce address {:p}", addr.as_paddr());
}
```

理论上 `BounceAddr` 已经确保了地址在 `BouncePool` 内，可能是发生了什么意料之外的情况，所以找不到直接 panic

**contains**

```rust
/// 检查给定的物理地址是否在 Bounce Pool 的范围内
pub fn contains(&self, paddr: PhysAddr) -> bool {
    let base = self.paddr_base.as_paddr();
    let size = self.total_slots as usize * SLOT_SIZE as usize;

    base <= paddr && paddr < base + size
}
```

## Mapping

`Mapping` 在目前没有什么用处，为了未来可能需要支持 IOMMU 准备，目前只有一个 `IdentityBackend` 直接使用物理地址作为 DMA 地址

### Mapping

```rust
pub trait Mapping: Sync {
    fn map(&self, phys_addr: PhysAddr, size: usize) -> Result<DmaAddr, MemoryError>;
    fn unmap(&self, dma_addr: DmaAddr) -> Result<(), MemoryError>;

    fn prepare_for_device(&self, _dma_addr: DmaAddr) -> Result<(), MemoryError>;
    fn prepare_for_cpu(&self, _dma_addr: DmaAddr) -> Result<(), MemoryError>;
}
```

`map` 和 `unmap` 是最基本的映射和解除映射函数，而 `prepare_for_*` 是用于处理数据一致性的，在 x86 下没有这种问题，但是在其他平台中 DMA 控制器和 CPU 看到的内存可能存在数据不一致的问题

### Backend

`Backend` 是实现了 `Mapping` trait 的结构的不可变引用的封装，这里 Rust 会使用胖指针（数据指针 + vtable指针）来保存

```rust
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Backend(&'static dyn Mapping);

impl Backend {
    pub const fn new(mapping: &'static dyn Mapping) -> Self {
        Self(mapping)
    }
}
```

另外还为 `Backend` 实现了自动解引用

```rust
impl Deref for Backend {
    type Target = dyn Mapping;

    fn deref(&self) -> &Self::Target {
        self.0
    }
}
```

在定义一下 `IdentityMapping` 并导出符号

```rust
pub struct IdentityMapping;

static IDENTITY_MAPPING: IdentityMapping = IdentityMapping;

#[unsafe(export_name = "dma_backend_identity")]
pub static IDENTITY_BACKEND: Backend = Backend::new(&IDENTITY_MAPPING);
```

## Sync

`Sync` 用来用同步软件映射的缓冲区，所以设计的类型只有 `Bounce` 和 `ScatterGather` 两种

```rust
pub enum Sync {
    None,
    Bounce(BounceAddr, SlotMeta, usize),
    ScatterGather(EntryList, Option<usize>),
}
```

### Bounce

首先是一个 `copy` 函数封装 `memcpy` 并处理同步方向的不同

```rust
/// 复制数据
///
/// `origin`: 原始数据的虚拟地址
///
/// `bounce`: Bounce Buffer 的虚拟地址
///
/// `size`: 需要复制的数据大小
///
/// `direction`: 数据传输的方向，仅支持 `ToDevice` 和 `FromDevice，不支持` `Bidirectional`
const fn copy(origin: *mut u8, bounce: *mut u8, size: usize, direction: Direction) {
    unsafe {
        match direction {
            Direction::ToDevice => copy_nonoverlapping(origin, bounce, size),
            Direction::FromDevice => copy_nonoverlapping(bounce, origin, size),
            _ => unreachable!(),
        }
    }
}
```

在 `copy` 的基础上，实现了 `sync` 来根据物理地址转换到虚拟地址的预处理和错误处理

```rust
/// 同步 Bounce Buffer 与原始缓冲区
///
/// `addr`: Bounce Buffer 的物理地址
///
/// `bounce`: Bounce Buffer 的元数据
///
/// `size`: 需要同步的数据大小
///
/// `direction`: 数据传输的方向，仅支持 `ToDevice` 和 `FromDevice，不支持 `Bidirectional`
pub fn sync(
    addr: BounceAddr,
    bounce: &SlotMeta,
    size: usize,
    direction: Direction,
) -> Result<(), MemoryError> {
    let pool = BouncePool::get_pool();

    let addr_start = addr.align_to_slot();
    let device_addr = pool.get_ptr(addr_start);

    let offset = addr
        .offset_from(addr_start)
        .ok_or(MemoryError::InvalidPhysicalAddress(addr.as_paddr()))?;
    let size = size.min(bounce.origin_size() - offset);

    let origin_virt = bounce
        .origin_addr()
        .try_to_virt()
        .ok_or(MemoryError::UnavailableFrame);

    if let Ok(cpu_addr) = origin_virt {
        let bounce = unsafe { (device_addr.byte_offset(offset as isize)).as_ptr() };

        Self::copy((cpu_addr + offset).as_mut_ptr(), bounce, size, direction);
    } else {
        // 如果物理地址无法直接映射到内核虚拟地址空间，我们需要通过分配一个新的页面来进行 Bounce Buffer 的映射
        let mut copied = 0;
        while copied < size {
            let origin_addr = bounce.origin_addr() + offset + copied;
            let origin_offset = origin_addr.page_offset();
            let chunk = (size - copied).min(ArchPageTable::PAGE_SIZE - origin_offset);
            let frame_options = FrameAllocOptions::new()
                .fixed(origin_addr.to_frame_number(), FrameOrder::new(0));

            let page = PageAllocOptions::new(frame_options).allocate()?;
            let cpu_addr = page.start_addr();

            let bounce =
                unsafe { (device_addr.byte_offset((offset + copied) as isize)).as_ptr() };

            Self::copy(
                (cpu_addr + origin_offset).as_mut_ptr(),
                bounce,
                chunk,
                direction,
            );

            vfree(cpu_addr)?;
            copied += chunk;
        }
    }
    Ok(())
}
```

如果物理地址没有映射到内核虚拟地址空间，那就临时创建一个映射，实现比较粗糙，凑合着用

### ScatterGather

对于分散到缓冲区，需要逐个访问，如果使用了 Bounce 缓冲区则需要调用 `BounceSync::sync` 来同步

```rust
pub fn sync(
    entries: &mut EntryList,
    n_entries: Option<usize>,
    direction: Direction,
) -> Result<(), MemoryError> {
    let mut iter = entries.iter_mut();
    let iter: &mut dyn Iterator<Item = &mut EntryList> = if let Some(n) = n_entries {
        &mut iter.take(n)
    } else {
        &mut iter
    };

    let pool = BouncePool::get_pool();
    for entry in iter {
        let paddr = entry.phys_addr().ok_or(MemoryError::UnavailableFrame)?;

        if let Some(addr) = BounceAddr::new(paddr) {
            // 如果当前 EntryList 的原物理地址在 Bounce Pool 中，说明需要进行同步
            let slot = pool.clone_slot(addr);
            BounceSync::sync(addr, &slot, entry.size(), direction)?;
        }
        // 原物理地址没有对应 bounce slot，说明它是直接映射的。
    }

    Ok(())
}
```

## Scatter Gather

单开了一部分来介绍这种结构，（只要设备支持）它可以使用物理不连续的缓冲区来发起 DMA 请求，其中的数据结构并不是常见的双向链表，而是通过数组和单向链表结合的形式，兼顾了效率和灵活性

驱动需要先通过 API 创建描述结构，然后自己把基础信息填好后交给框架去映射 / 同步

单个节点的数据结构如下：

```rust
#[repr(C)]
pub struct EntryList {
    entry_type: EntryTypeRaw,
    offset: usize,
    size: usize,
    dma_addr: DmaAddr,
}
```

### EntryType

`EntryTypeRaw` 是 `usize` 的包装，因为这一结构同时描述了页信息和标记信息，如果分开定义会占用较多空间

```rust
#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct EntryTypeRaw(usize);

pub enum EntryType {
    Array(Option<NonNull<Frame>>),
    Chain(NonNull<EntryList>),
}

mod tags {
    /// 表示当前 Entry 是一个指向下一个 Array 的链表
    pub const CHAIN: usize = 0b01;
    /// 表示整个链表的结束
    pub const END: usize = 0b10;
    pub const MASK: usize = 0b11;
}
```

`CHAIN` 标记表示当前节点本身不含信息，其指针指向下一组数组。

`END` 只能和 `Array(_)` 一起出现，即最后一项可以是空的 `Array(None)` 也能是被使用的 `Array(Some(_))`

两个类型实现了互转

```rust
impl EntryTypeRaw {
    const ZERO: Self = Self(0);

    const fn new(ptr: usize, flags: usize) -> Self {
        Self((ptr & !tags::MASK) | (flags & tags::MASK))
    }

    /// 获取 EntryType
    pub const fn get(&self) -> (EntryType, bool) {
        let ptr = (self.0 & !tags::MASK) as *mut ();

        let entry_type = if self.is_chain() {
            EntryType::Chain(
                NonNull::new(ptr)
                    .expect("A chain with a null pointer in EntryList")
                    .cast(),
            )
        } else {
            EntryType::Array(NonNull::new(ptr as *mut Frame))
        };
        (entry_type, self.is_end())
    }

    pub const fn is_chain(&self) -> bool {
        (self.0 & tags::CHAIN) != 0
    }

    pub const fn is_end(&self) -> bool {
        (self.0 & tags::END) != 0
    }

    const fn is_null(&self) -> bool {
        (self.0 & !tags::MASK) == 0
    }
}

impl EntryType {
    pub fn to_raw(&self, flags: usize) -> EntryTypeRaw {
        let flags = if is_end { tags::END } else { 0 };
        match self {
            Self::Array(Some(ptr)) => EntryTypeRaw::new(ptr.addr().get(), flags),
            Self::Array(None) => EntryTypeRaw::new(0, flags),
            Self::Chain(ptr) => EntryTypeRaw::new(ptr.addr().get(), flags | tags::CHAIN),
        }
    }
}
```

### EntryList

#### 基础实现

在 `EntryList` 的实现中，首先需要一些工具函数

```rust
pub const fn clear(&mut self) {
    self.entry_type = EntryTypeRaw::EMPTY;
    self.offset = 0;
    self.size = 0;
    self.dma_addr = DmaAddr::new(0);
}

/// 设置 END 标记，仅供内部使用
fn mark_end(&mut self) {
    let raw = self.entry_type.0;
    self.entry_type = EntryTypeRaw::new(raw, (raw & tags::MASK) | tags::END);
}

/// 强行设置当前 entry 为 CHAIN，指向下一个 segment
///
/// # Safety
///
/// `next` 必须是通过 `EntryList::create_segment` 分配的，并且没有被释放过
///
/// 需要确保当前 entry 不含有效数据，否则会导致内存泄漏
unsafe fn force_set_chain(&mut self, next: NonNull<EntryList>) {
    self.entry_type = EntryType::Chain(next).to_raw(false);
}

/// 分配 `capacity` 个连续 EntryList。
///
/// 前 capacity-1 个初始化为零（frame=0，无标志），最后一个设 END。
pub fn create_segment(capacity: usize) -> Option<NonNull<EntryList>> {
    let size = NonZeroUsize::new(capacity.checked_mul(size_of::<EntryList>())?)?;
    let head = kmalloc::<EntryList>(size)?;

    let entries = unsafe { core::slice::from_raw_parts_mut(head.as_ptr(), capacity) };
    for entry in entries.iter_mut() {
        entry.clear();
    }
    // 最后一个 slot 标记数组结束
    if capacity > 0 {
        entries[capacity - 1].mark_end();
    }

    Some(head)
}

/// 设置页帧（Array 类型），保留 END 标志。
///
/// 仅在当前 EntryList 为 Array(None) 时成功
pub fn set_frame(&mut self, frame: NonNull<Frame>, offset: usize, size: usize) -> Option<()> {
    if let (EntryType::Array(None), is_end) = self.get_type() {
        self.entry_type = EntryType::Array(Some(frame)).to_raw(is_end);
        self.offset = offset;
        self.size = size;
        Some(())
    } else {
        None
    }
}

/// 从线性映射地址设置缓冲区，自动拆分为 Frame + offset。
///
/// 仅在当前 EntryList 为 Array(None) 时成功
pub fn set_buffer(&mut self, buffer: *const u8, size: usize) -> Option<()> {
    let vaddr = VirtAddr::new(buffer.addr());
    let page_vaddr = vaddr.page_align_down();
    let offset = vaddr.page_offset();

    debug_assert!(
        page_vaddr >= KLINEAR_BASE && vaddr + size < KLINEAR_END,
        "set_buffer only accepts linear-mapped addresses"
    );

    let paddr = PhysAddr::new(page_vaddr - KLINEAR_BASE);
    let frame = Frame::get_raw(paddr.to_frame_number());

    self.set_frame(frame, offset, size)
}

pub const fn get_type(&self) -> (EntryType, bool) {
    self.entry_type.get()
}

pub const fn is_chain(&self) -> bool {
    self.entry_type.is_chain()
}

pub const fn is_end(&self) -> bool {
    self.entry_type.is_end()
}

pub const fn dma_addr(&self) -> DmaAddr {
    self.dma_addr
}

pub const fn set_dma_addr(&mut self, dma_addr: DmaAddr) {
    self.dma_addr = dma_addr;
}

pub const fn size(&self) -> usize {
    self.size
}

pub const fn offset(&self) -> usize {
    self.offset
}

pub fn virt_addr(&self) -> Option<VirtAddr> {
    self.phys_addr()?
        .try_to_virt()
        .map(|vaddr| vaddr + self.offset)
}

pub fn phys_addr(&self) -> Option<PhysAddr> {
    match self.get_type().0 {
        EntryType::Array(Some(frame)) => unsafe {
            Some(frame.as_ref().start_addr() + self.offset)
        },
        _ => None,
    }
}

pub fn iter(&self) -> EntryIter<'_> {
    EntryIter { next: Some(self) }
}

pub fn iter_mut(&mut self) -> EntryIterMut<'_> {
    EntryIterMut { next: Some(self) }
}

/// 尝试释放 bounce buffer，如果当前 entry 不是 bounce buffer，则不做任何操作
fn try_free_bounce(frame: NonNull<Frame>, offset: usize) {
    let pool = BouncePool::get_pool();
    let paddr = unsafe { frame.as_ref() }.start_addr() + offset;
    if let Some(bounce_addr) = BounceAddr::new(paddr) {
        pool.deallocate(bounce_addr);
    }
}

/// 释放当前 array 的内存，不负责释放 bounce buffer
///
/// `head`: array 的首地址
///
/// # Safety
///
/// 必须已经释放了所有的 bounce buffer，否则会导致内存泄漏
unsafe fn destroy_array(head: NonNull<EntryList>) {
    if let Err(e) = kfree(head) {
        printk!(
            "WARN: Failed to free EntryList segment at {:p}: {:?}",
            head.as_ptr(),
            e
        );
    }
}
```

除了 `create_segment` 之外没有什么复杂的逻辑，除了这些之外还以一个相对复杂的 `destory_all` 用于将当前节点开始往后的所有节点都释放掉，但如果前面还有节点连着的话会造成悬垂指针所以标记为 `unsafe`

```rust
/// 沿 CHAIN 遍历释放所有 segment。
///
/// # Safety
///
/// `head` 必须是通过 `EntryList::create_segment` 分配的，并且没有被释放过
pub unsafe fn destroy_all(head: NonNull<EntryList>) {
    let mut current = head;
    let mut head = head;
    loop {
        let entry = unsafe { current.as_ref() };

        current = match entry.get_type() {
            (entry_type, true) => {
                if let EntryType::Array(Some(frame)) = entry_type {
                    Self::try_free_bounce(frame, entry.offset);
                }
                unsafe { Self::destroy_array(head) };
                break;
            }
            (EntryType::Chain(next_head), false) => {
                unsafe { Self::destroy_array(head) };
                head = next_head;
                head
            }
            (EntryType::Array(Some(frame)), false) => {
                Self::try_free_bounce(frame, entry.offset);
                unsafe { current.add(1) }
            }
            (EntryType::Array(None), false) => unsafe { current.add(1) },
        };
    }
}
```

其中，如果当前项是 `END` 且有有效 buffer，则需要先检查其是否是 bounce buffer 并释放，再释放整个 Array

#### 迭代器

另外还实现了迭代器方便遍历，为了通用即使是空的项也会被返回

```rust
pub struct EntryIter<'a> {
    next: Option<&'a EntryList>,
}

impl<'a> Iterator for EntryIter<'a> {
    type Item = &'a EntryList;

    fn next(&mut self) -> Option<Self::Item> {
        let entry = self.next.take()?;

        self.next = match entry.get_type() {
            (EntryType::Array(None), true) => None,
            (EntryType::Array(Some(_)), true) => {
                unreachable!("An Array(Some) entry cannot be the end of the list")
            }
            (EntryType::Array(_), false) => unsafe { (entry as *const EntryList).add(1).as_ref() },
            (EntryType::Chain(next_head), false) => Some(unsafe { next_head.as_ref() }),
            (EntryType::Chain(_), true) => {
                unreachable!("A chain entry cannot be the end of the list")
            }
        };
        Some(entry)
    }
}

pub struct EntryIterMut<'a> {
    next: Option<&'a mut EntryList>,
}

impl<'a> Iterator for EntryIterMut<'a> {
    type Item = &'a mut EntryList;

    fn next(&mut self) -> Option<Self::Item> {
        let entry = self.next.take()?;

        self.next = match entry.get_type() {
            (EntryType::Array(None), true) => None,
            (EntryType::Array(Some(_)), true) => {
                unreachable!("An Array(Some) entry cannot be the end of the list")
            }
            (EntryType::Array(_), false) => unsafe { (entry as *mut EntryList).add(1).as_mut() },
            (EntryType::Chain(mut next_head), false) => Some(unsafe { next_head.as_mut() }),
            (EntryType::Chain(_), true) => {
                unreachable!("A chain entry cannot be the end of the list")
            }
        };
        Some(entry)
    }
}
```

# 封装

## DmaHandle

`DmaHandle` 用于表示一段已映射的连续内存，可以通过指针的方式传递给 C 侧使用

```rust
pub struct DmaHandle {
    device: NonNull<Device>,
    source: Source,
    vaddr: VirtAddr,
    paddr: PhysAddr,
    dma_addr: DmaAddr,
    size: usize,
    direction: Direction,
}
```

一个 DMA 映射会在 `DmaHandle` 创建时建立，先通过 Source 层分配，再通过 Mapping 层映射

```rust
pub fn new(
    device: &mut Device,
    source: Source,
    vaddr: Option<VirtAddr>,
    size: usize,
    direction: Direction,
) -> Result<Self, MemoryError> {
    let (acquired_vaddr, paddr) = source.acquire(device)?;
    let vaddr = acquired_vaddr
        .or(vaddr)
        .ok_or(MemoryError::InvalidVirtualAddress(VirtAddr::new(0)))?;

    match device.backend.map(paddr, size) {
        Ok(dma_addr) => Ok(Self {
            device: NonNull::from_ref(device),
            source,
            vaddr,
            paddr,
            dma_addr,
            size,
            direction,
        }),
        Err(e) => {
            printk!(
                "WARNING: Failed to release source after backend.map() failed: {:?}\n",
                e
            );
            Err(e)
        }
    }
}
```

### 释放

释放需要先同步，然后取消映射，再真正释放缓冲区

```rust
pub fn deallocate(self) -> Result<(), MemoryError> {
    let device = unsafe { self.device.as_ref() };

    if let Err(e) = self.sync_for_cpu(0, self.size) {
        printk!(
            "WARNING: Failed to sync DMA buffer for CPU before deallocation: {:?}\n",
            e
        );
    }

    let mut result = device.backend.unmap(self.dma_addr);
    if let Err(e) = self.source.release(device, self.vaddr, self.paddr) {
        if result.is_ok() {
            result = Err(e);
        }
    }
    result
}
```

### 同步

为了提供足够的自由度同时具备易用性，同时支持了分开和集成的软件同步、一致性维护

```rust
/// 获取同步类型以调用 `sync_for_device` 或 `sync_for_cpu`
///
/// `base`: 需要同步的物理基地址
///
/// `offset`: 偏移量
///
/// `size`: 需要同步的大小，如果为 `None`，则使用整个缓冲区的大小
fn sync_type(&self, base: PhysAddr, offset: usize, size: usize) -> Result<Sync, MemoryError> {
    match self.source {
        Source::Coherent { .. } | Source::Direct { .. } | Source::Pool { .. } => Ok(Sync::None),
        Source::SoftwareIotlb { .. } => {
            let pool = BouncePool::get_pool();
            let addr = BounceAddr::new(base)
                .ok_or(MemoryError::InvalidPhysicalAddress(base + offset))?;
            let slot = pool.clone_slot(addr);

            let size = (offset + size).min(slot.origin_size()) - offset;

            Ok(Sync::Bounce(addr, slot, size))
        }
    }
}

/// 使更新对 CPU 可见
///
/// 这通常在 DMA 写入数据后调用，以确保 CPU 可以看到最新的数据
#[inline]
pub fn prepare_for_cpu(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
    let device = unsafe { self.device.as_ref() };
    device.backend.prepare_for_cpu(self.dma_addr + offset, size)
}

/// 使更新对设备可见
///
/// 这通常在 CPU 写入数据后调用，以确保设备可以看到最新的数据
#[inline]
pub fn prepare_for_device(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
    let device = unsafe { self.device.as_ref() };
    device
        .backend
        .prepare_for_device(self.dma_addr + offset, size)
}

/// 向设备同步数据
pub fn sync_range_for_device(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
    self.sync_type(self.paddr, offset, size)?
        .sync_for_device()?;

    self.prepare_for_device(offset, size)
}

pub fn sync_for_cpu(&self, offset: usize, size: usize) -> Result<(), MemoryError> {
    self.prepare_for_cpu(offset, size)?;

    self.sync_type(self.paddr, offset, size)?.sync_for_cpu()
}
```

## Device

### 结构

```rust
pub struct Device {
    pub(super) constraints: Constraints,
    pub(super) backend: Backend,
    pub(super) coherency: bool,

    pub(super) coherent_allocator: Option<CoherentAllocator>,
}
```

`constraints` 定义了设备要求的所有限制：

```rust
#[repr(C)]
#[derive(Clone)]
pub struct Constraints {
    pub mask: usize,
    pub coherent_mask: usize,
    pub boundary_mask: usize,
    pub max_segment_size: u32,
    pub max_segments: u16,
}
```

`backend` 选择了映射的方式

`coherency` 决定了 CPU 的页缓存方式

`coherent_allocator` 则是页的分配器

### 分配&映射

`Device` 提供了几种分配方式的封装

**coherent**

```rust
/// 根据给定的字节大小分配一块对齐到 FrameOrder 的 DMA 内存，返回 CPU 虚拟地址和 DMA 地址。
pub fn alloc_coherent(&mut self, size: usize) -> Result<DmaHandle, MemoryError> {
    DmaHandle::new(
        self,
        Source::Coherent { size },
        None,
        size,
        Direction::Bidirectional,
    )
}
```

**pool**

```rust
pub fn create_pool(
    &self,
    name: &'static CStr,
    object_size: NonZeroU16,
    align: usize,
) -> Option<NonNull<Pool>> {
    Pool::new(name, object_size, align, &self.constraints, self.coherency)
}

pub fn alloc_pool(&mut self, pool: NonNull<Pool>) -> Result<DmaHandle, MemoryError> {
    let size = unsafe { pool.as_ref() }.object_size();
    DmaHandle::new(
        self,
        Source::Pool { pool },
        None,
        size,
        Direction::Bidirectional,
    )
}
```

**map_sg**

```rust
pub fn map_sg(
    &mut self,
    entries: &mut EntryList,
    n_entries: usize,
) -> Result<usize, MemoryError> {
    let mut mapped = 0;
    let mut result = Ok(());

    {
        let iter = entries.iter_mut();

        for entry in iter.take(n_entries) {
            if entry.is_empty() {
                continue;
            }

            if self.constraints.max_segments > 0
                && mapped >= self.constraints.max_segments as usize
            {
                result = Err(MemoryError::ViolateConstraint);
                break;
            }

            let paddr = match entry.phys_addr() {
                Some(phys) => phys,
                None => {
                    result = Err(MemoryError::OtherError);
                    break;
                }
            };

            let size = entry.size();
            let need_bounce = (paddr.as_usize() & !self.constraints.mask) != 0
                || self.constraints.crosses_boundary(paddr, size)
                || self.constraints.exceeds_max_segment_size(size);

            let source = if !need_bounce {
                Source::Direct { paddr, size }
            } else {
                Source::SoftwareIotlb { paddr, size }
            };

            let dma_paddr = match source.acquire(self) {
                Ok((_, paddr)) => paddr,
                Err(e) => {
                    result = Err(e);
                    break;
                }
            };

            let dma_addr = match self.backend.map(dma_paddr, size) {
                Ok(addr) => addr,
                Err(e) => {
                    let _ = source.release(self, VirtAddr::new(0), dma_paddr);
                    result = Err(e);
                    break;
                }
            };

            entry.set_dma_addr(dma_addr);
            mapped += 1;
        }
    }

    match result {
        Ok(_) => Ok(mapped),
        Err(e) => {
            // 出错时，回滚已映射的段，此时不需要同步任何数据
            self.unmap_sg(entries, mapped)?;
            Err(e)
        }
    }
}
```

**map_single**

```rust
/// 流式 DMA 单段映射。
///
/// 物理连续且在 DMA mask 内 → 直接映射；否则分配 bounce buffer。
pub fn map_single<T>(
    &mut self,
    ptr: NonNull<T>,
    size: usize,
    direction: Direction,
) -> Result<DmaHandle, MemoryError> {
    let vaddr = VirtAddr::new(ptr.addr().get());

    let source = direct::translate(vaddr, size)
        .map_or(Err(MemoryError::UnavailableFrame), |paddr| {
            Ok(Source::Direct { paddr, size })
        })
        .or_else(|_| -> Result<Source, MemoryError> {
            let pt = current_root_pt();
            let paddr = PageTableOps::<ArchPageTable>::translate(pt, vaddr)
                .ok_or(MemoryError::UnavailableFrame)?;
            Ok(Source::SoftwareIotlb { paddr, size })
        })?;

    let handle = DmaHandle::new(self, source, Some(vaddr), size, direction)?;
    if matches!(direction, Direction::ToDevice | Direction::Bidirectional) {
        if let Err(e) = handle.sync_range_for_device(0, size) {
            if let Err(rollback_error) = handle.deallocate() {
                printk!(
                    "WARNING: Failed to roll back single DMA mapping after sync error: {:?}\n",
                    rollback_error
                );
            }
            return Err(e);
        }
    }
    Ok(handle)
}
```

### 释放&解除映射

**unmap_sg**

```rust
pub fn unmap_sg(&self, entries: &mut EntryList, n_entries: usize) -> Result<(), MemoryError> {
        let mut result = Ok(());

        let iter = entries.iter_mut();

        for entry in iter.take(n_entries) {
            if entry.is_empty() {
                continue;
            }

            let dma_addr = entry.dma_addr();
            if dma_addr.as_usize() == 0 {
                continue;
            }

            let paddr = entry.phys_addr();

            if let Err(e) = self.backend.unmap(dma_addr) {
                printk!(
                    "WARNING: Failed to unmap DMA address {:#x}: {:?}\n",
                    dma_addr.as_usize(),
                    e
                );
                if result.is_ok() {
                    result = Err(e);
                }
                continue;
            }

            entry.set_dma_addr(DmaAddr::new(0));

            if let Some(paddr) = paddr {
                let pool = BouncePool::get_pool();
                if let Some(addr) = BounceAddr::new(paddr) {
                    pool.deallocate(addr);
                }
            }
        }

        result
    }
```

**unmap_single**

```rust
/// 流式 DMA 单段解映射。
pub fn unmap_single(&self, dma_handle: DmaHandle) -> Result<(), MemoryError> {
    let mut result = if matches!(
        dma_handle.direction(),
        Direction::FromDevice | Direction::Bidirectional
    ) {
        dma_handle.sync_range_for_cpu(0, dma_handle.size())
    } else {
        Ok(())
    };

    if let Err(e) = dma_handle.deallocate() {
        if result.is_err() {
            printk!(
                "WARNING: Failed to deallocate single DMA mapping after sync error: {:?}\n",
                e
            );
        } else {
            result = Err(e);
        }
    }
    result
}
```

### 同步

**sync_sg_for_device**

```rust
pub fn sync_sg_for_device(
    &self,
    entries: &mut EntryList,
    n_entries: usize,
) -> Result<(), MemoryError> {
    sync_sg_for_device(entries, n_entries)?;

    for entry in entries.iter().take(n_entries) {
        let dma_addr = entry.dma_addr();
        if dma_addr.as_usize() != 0 {
            self.backend.prepare_for_device(dma_addr, entry.size())?;
        }
    }

    Ok(())
}
```

**sync_sg_for_cpu**

```rust
pub fn sync_sg_for_cpu(
    &self,
    entries: &mut EntryList,
    n_entries: usize,
) -> Result<(), MemoryError> {
    for entry in entries.iter().take(n_entries) {
        let dma_addr = entry.dma_addr();
        if dma_addr.as_usize() != 0 {
            self.backend.prepare_for_cpu(dma_addr, entry.size())?;
        }
    }

    sync_sg_for_cpu(entries, n_entries)
}
```
