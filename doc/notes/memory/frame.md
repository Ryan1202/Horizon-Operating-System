# 如何描述一个物理页

在一个内核中，一个物理页除了通过页表映射以外，还需要非常多额外的信息来描述和使用，例如引用计数和锁解决并发问题、类型标注来区分不同用途，所以内核还需要另外为物理页创建元数据来补充这些信息.

对于 32 位操作系统，最多可能有 1024 * 1024 = 1,048,576 个物理页，如果对齐到半个缓存行也就是 32 字节，一共需要 32 MB，是可以接受的消耗

```rust
static FRAMES: [Frame; 1 << 20] = unsafe { mem::zeroed() }
```

这里语言强制要求初始化，所以都设置为0

## 物理页的状态

这里我们根据物理页的状态、所有者可以大致划为以下几种：

```mermaid
stateDiagram-v2
	[*] --> Free: E820扫描
	
	Free --> Buddy: 加入物理页管理（buddy）
	Buddy --> Anonymous: 分配
	
	Anonymous --> Slub: 加入对象缓存池
	Anonymous --> 其他: 大块分配
	
	Slub --> Anonymous: 释放对象缓存池
	其他 --> Anonymous: kfree
	
	Anonymous --> Buddy: 释放物理页
	Buddy --> Buddy: 合并、切分物理页组
```

## 物理页元数据

这里使用了 `AtomicU8` 原子类型来存储物理页的状态，可以原子操作避免使用锁

```rust
struct Frame {
    refcount: FrameRc,          // 8 字节：引用计数
    data: SyncUnsafeCell<FrameData>,
    tag: AtomicU8,              // 1 字节：当前状态
}
```

### 引用计数

这是一个用来保存引用计数的类型

```rust
#[repr(transparent)]
pub struct FrameRc {
    count: AtomicUsize,
}
```

使用 `AtomicUsize` 原子变量来存引用计数，同时还用来区分独占引用和共享引用，拥有独占引用就可以无锁化使用这个物理页而不用担心竞争的问题了

由于引用数至少是从 1 开始，那么我们就选择 0 来表示独占引用。当然这时候又会碰到一个问题：这个物理页还没有分配的时候应该是多少呢？这里我还是使用 0，有两层原因：

- 初始化元数据时是全部置 0 的，选择 0 就无需修改状态了
- 未分配时也可以看作分配器独占了这个物理页，分配器自己也有个粗粒度的锁解决竞争问题，所以不用担心同时存在多个独占引用。至于分配时，分配器也是以独占的形式转交给调用方，也就不需要修改了

以下是实现：

```rust
impl FrameRc {
    const EXCLUSIVE: usize = 0;
    
    fn update(&self, f: impl Fn(usize) -> Option<usize>) -> Option<usize> {
        self.count
            .fetch_update(Ordering::Release, Ordering::Acquire, f)
            .ok()
    }

    fn acquire(&self) -> Option<()> {
        self.update(|value| (value != Self::EXCLUSIVE).then_some(value + 1))
            .map(|_| ())
    }

    fn release(&self) -> Option<usize> {
        self.update(|value| {
            if value == Self::EXCLUSIVE {
                Some(value) // 已经是独占状态，不修改
            } else {
                Some(value - 1)
            }
        })
    }

    pub fn get(&self) -> usize {
        self.count.load(Ordering::Relaxed)
    }

    pub fn is_exclusive(&self, frame: &Frame) -> bool {
        self.get() == Self::EXCLUSIVE && !matches!(frame.get_tag(), FrameTag::Buddy)
    }
}
```

这里使用到引用计数时基本上只会是使用共享引用的时候，独占引用最多调用一次 `release` 释放（虽然实际上也不会修改）。还有一点，分配器是不会调用 `is_exclusive` 的，因为他只使用独占引用，为了避免其他程序误使用了未分配的页，加上了额外的检查

### FrameData

为了尽量减少元数据的内存占用以及规避rust的enum无法控制内存布局的问题，这里使用 `union` 联合体来将不会发生冲突的几种元数据放到同一块内存地址里

```rust
union FrameData {
    pub unused: (),
    pub range: FrameRange,
    pub(super) buddy: ManuallyDrop<Buddy>,
    pub(super) slub: ManuallyDrop<Slub>,
    pub anonymous: ManuallyDrop<Anonymous>,
}
```

`unused` 和 `range` 很好理解，不多解释了。

 `buddy` 和 `slub` 是物理页分配器和小内存（对象）分配器各自需要的数据，由于仅可被内部使用，所以可见程度设为`pub(super)` 。

`anonymous` 参考的Linux的叫法，是刚从 `buddy` 分配过来时的默认状态，因为没有任何其他信息，所以是匿名的（anonymous）

### FrameTag

用来标记物理页的状态，这里由于默认初始化为0，所以指定 `Uninited` 为默认状态的 0。

```rust
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FrameTag {
    /// 未配置（比如为一组页中非首个页）
    Uninited = 0,
    HardwareReserved = 1,
    SystemReserved = 2,
    /// `Free` 是初始化时使用的临时类型，分配器初始化完成后不应再出现该类型的页
    Free = 3,
    /// `Anonymous` 用于标识该组页已被分配，但没有标识类型
    Anonymous = 4,
    Buddy = 5,
    Slub = 6,
    /// 跨越单个物理页的大页中，使用头一个页存放元数据，其余页使用 `Tail` 标记
    Tail = 7,
}
```

这里 `#[repr(u8)]` 指定布局按照u8类型来，用来配合上 `AtomicU8`

系统在刚启动的时候读取内存布局，将可用的物理页都标记为 `Free` （当然为了优化性能实际上只设置了每一块内存的第一个物理页，使用 `range` 补充），由物理页分配器 Buddy 来处理这些 `Free` 的物理页。

另外在实际使用中，常常会出现一组物理页打包起来组成更大的大页来使用的情况，这里使用第一个物理页元数据代表整个大页，其余的页标记为 `Tail`，使用 `range` 标记范围。

### 为什么使用 AtomicU8 表示 FrameTag

多核系统中，不同 CPU 可能同时访问同一个 Frame：

```mermaid
sequenceDiagram
    participant CPU0
    participant Frame
    participant CPU1
    
    par 并发访问
        CPU0->>Frame: 准备写入 data...
        Frame->>CPU1: 读取 data...
        Frame->>CPU0: 写入完成
    end
    
    Note over Frame: 可能读到旧值！
```

使用原子类型可以避免加锁：

```rust
fn get_tag(&self) -> FrameTag {
    self.tag.load(Ordering::Acquire)  // 保证后续读取不被重排到前面
}

fn set_tag(&mut self, tag: FrameTag) {
    self.tag.store(tag, Ordering::Release);  // 保证前面写入不被重排到后面
}
```

## FrameOrder

为了方便 Buddy 的管理，物理页数统一使用物理页的阶数 order 来表示，也就是说数量都会被要求对齐到 2 的幂，最大值为 10，也就是 1024 个物理页（4MB）

```rust
pub const MAX_ORDER: FrameOrder = FrameOrder(10);

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct FrameOrder(u8);
```



## FrameNumber

这里还专门使用了一个新的类型来表示物理页号，省去直接使用地址带来的对齐、溢出等问题

```rust
#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
pub struct FrameNumber(usize);
```

给它实现了一些比较常用的如比较、加减法、复制等功能

然后是一些独有的功能：
```rust
impl FrameNumber {
    pub const fn new(num: usize) -> Self {
        FrameNumber(num)
    }

    pub const fn get(&self) -> usize {
        self.0
    }

    /// 获取当前物理页指针对应的FrameNumber
    pub fn from_frame(frame: NonNull<Frame>) -> Self {
        let frame_number = unsafe { frame.as_ref() }.to_frame_number();
        FrameNumber(frame_number.0)
    }

    /// 计算两个物理页号之间的差
    pub const fn count_from(self, other: FrameNumber) -> usize {
        self.0.abs_diff(other.0)
    }

    /// 向下对齐到 order
    pub const fn align_down(self, order: FrameOrder) -> FrameNumber {
        let mask = (1 << order.get()) - 1;
        FrameNumber(self.0 & !mask)
    }
}
```

# Zone —— 物理内存的划分

## 为什么需要划分区域

在内核中，将物理内存大致划为以下几种区域：

| 区域       | 地址范围             | 内容                                                         |
| ---------- | -------------------- | ------------------------------------------------------------ |
| 保留       | 0 - 1MB              | 超过 1/3 是 BIOS 使用的，由于切的比较乱，所以只用来当内核主线程的栈和GDT, IDT这些乱七八糟的描述符，不可动态分配 |
| 线性映射区 | 1MB - 896MB（32位）  | 内核主要使用的空间，由于预先映射好了，使用起来也比较快       |
| MEM32      | 896MB - 4GB （32位） | 内核的临时映射、MMIO以及用户态使用的内存，需要动态创建映射   |

本来在 Linux 中还有一个 `DMA` Zone，由于主要是给 ”远古“ 的 ISA DMA 使用的，就舍弃掉减少复杂度了，实在要用就从前 1MB 里面抠一点空间出来用

定义如下：

```rust
#[repr(u8)]
#[derive(Clone, Copy, Debug)]
pub enum ZoneType {
    /// 内核线性映射区：32位下低于896MB，64位下高于4GB
    LinearMem = 0,
    /// 32位可寻址内存：
    /// 32位下为高于线性映射区的内存，64位下为32位地址范围内内存
    MEM32 = 1,
}
```

由于我希望这个类型放到 C 下也能识别，所以手动指定了每个类型的值。

```rust
pub const RESERVED_END: FrameNumber = FrameNumber::new(0x100);

impl ZoneType {
    // `index()` 和 `from_index()` 比较简单，略
    
    pub const fn range(&self) -> (PhysAddr, PhysAddr) {
        match self {
            ZoneType::LinearMem => (PhysAddr::new(0x100000), PhysAddr::new(0x30000000)),
            ZoneType::MEM32 => (PhysAddr::new(0x30000000), PhysAddr::new(usize::MAX)),
        }
    }
    
	pub const fn from_address(addr: PhysAddr) -> Self {
        let frame_number = addr.to_frame_number().get();
        assert!(frame_number >= RESERVED_END, "Low 1MiB memory is reserved");

        if frame_number < 0x30000 {
            ZoneType::LinearMem
        } else {
            ZoneType::MEM32
        }
    }
}
```



比较重要的一个函数是 `from_address` 通过将地址转换为 帧号 再判断属于哪个 Zone

# 内存管理初始化

## init_memory

负责把 Rust 和 C 代码粘在一起：

```c
void init_memory(void) {
	uint32_t ards_addr	  = ARDS_ADDR;
	uint32_t ards_nr_addr = ARDS_NR;
	uint16_t ards_nr	  = *((uint16_t *)ards_nr_addr); // ards 结构数
	ards				  = (struct ards *)ards_addr;	 // ards 地址

	page_init(ards, ards_nr, (size_t)&_kernel_start_phy);

	mem_caches_init();

	vmalloc_init();
}
```

## page_init

先看函数参数

```rust
#[unsafe(no_mangle)]
pub extern "C" fn page_init(blocks: *const E820Ards, block_count: u16, kernel_start: usize)
```

`blocks` 和 `blocks_count` 是之前 C 程序中读取的由引导程序提供的内存布局信息，`kernel_start` 和 `kernel_end` 则是内核程序的起始和结束地址

`*const E820Ards` 类型是一个指向 `E820Ards` 的不可变原始指针，`E820Ards` 则是按照标准格式定义的，`#[repr(C)]` 表示按照 C 的结构体字段布局规则来：

```rust
#[repr(C)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
}
```

然后是主体部分：

```rust
let kernel_range = (PhysAddr::new(kernel_start), unsafe {
    *PREALLOCATED_END_PHY.get()
});
Frame::init(blocks, block_count, kernel_range);

FRAME_MANAGER.init();
```

先调用 `Frame::init` 初始化页帧，然后由 `FRAME_MANAGER` 来初始化分配器

## Frame::init

帧元数据的初始化有一点麻烦，需要计算 e820 提供的 block 范围、内核定义的 Zone 范围（这里主要是 线性映射区 和 临时映射区 两个Zone）以及排除内核所在区域。

做的事情不复杂，就是代码看起来会有一点乱，需要考虑一点 corner case
```rust
/// 初始化页结构体数组，根据E820内存块信息划分内存，返回页结构体数组所需的总字节数
fn init(
    blocks: *const E820Ards,
    block_count: u16,
    kernel_range: (PhysAddr, PhysAddr),
) -> usize {
    let (kernel_start, kernel_end) = (
        kernel_range.0.to_frame_number(),
        kernel_range.1.to_frame_number(),
    );

    let mut last = FrameNumber::new(0);
    // 将每个可用内存块按Buddy的方式分割成块
    for i in 0..block_count {
        let block = unsafe { &*blocks.add(i as usize) };

        // 起始地址向后对齐，避免向前越界
        let start_addr = PhysAddr::new(block.base_addr as usize).page_align_up();
        let block_start = start_addr.to_frame_number();

        let length = block.length as usize - start_addr.page_offset();

        let block_end = block_start + frame_count(length);

        if last < block_start {
            // 填充上一个块和当前块之间的空洞为保留
            Self::fill_range(last, block_start, 2);
        }
        last = block_end;

        if block_start >= block_end {
            continue;
        }

        // 填充 [block_start, block_end) 范围
        if block_end <= kernel_start || block_start >= kernel_end {
            Self::fill_range(block_start, block_end, block.block_type);
        } else {
            // 内存块和内核有重叠部分
            let e820_type = block.block_type;

            // 前半部分可用
            if block_start < kernel_start {
                Self::fill_range(block_start, kernel_start, e820_type);
            }

            Self::fill_range(kernel_start, kernel_end, 0);

            // 后半部分可用
            if block_end > kernel_end {
                Self::fill_range(kernel_end, block_end, e820_type);
            }
        }
    }

    PAGE_INFO_SIZE
}
```

`fill_range` 负责填充指定（同类型）范围内的头一个页

```rust
/// 填充 [start, end) 范围的 `Frame`
///
/// 实际上只填写 start 一个 `Frame`
fn fill_range(start: FrameNumber, end: FrameNumber, e820_type: u32) {
    debug_assert!(start < end);

    let frame = unsafe { Self::get_raw(start).as_mut() };
    let mut range = FrameRange { start, end };
    let mut data = FrameData { range };

    let tag = match e820_type {
        0 => FrameTag::SystemReserved,
        1 => FrameTag::Free,
        2 | 3 | 4 => FrameTag::HardwareReserved,
        _ => FrameTag::BadMemory,
    };

    if range.end < RESERVED_END {
        // 跳过保留区的可用内存
        return;
    } else if range.start < RESERVED_END {
        // 跨越边界则调整起始地址
        range.start = RESERVED_END;
        data = FrameData { range };
    }

    let count = end.count_from(start);
    match tag {
        FrameTag::Free => {
            TOTAL_PAGES.fetch_add(count, Ordering::Relaxed);
        }
        FrameTag::SystemReserved => {
            TOTAL_PAGES.fetch_add(count, Ordering::Relaxed);
            ALLOCATED_PAGES.fetch_add(count, Ordering::Relaxed);
        }
        _ => {}
    }

    // 写入首个 page 描述
    unsafe { frame.replace(tag, data) };
}
```

这里还用到了一个函数 `Frame::replace` 先简单解释一下：这个函数会将 `tag` 和 `data` 一起替换，其中包括了对旧 data 的 Drop 操作

## E820

ACPI 标准中定义的 E820 结构如下：

| 偏移（字节） | 名字                | 描述             |
| ------------ | ------------------- | ---------------- |
| 0            | BaseAddrLow         | 基地址低32位     |
| 4            | BaseAddrHigh        | 基地址高32位     |
| 8            | LengthLow           | 长度低32位       |
| 12           | LengthHigh          | 长度高32位       |
| 16           | Type                | 地址块的内存类型 |
| 20           | Extended Attributes | 扩展属性         |

地址类型如下：

| 值   | 类型                  | 描述                                    |
| ---- | --------------------- | --------------------------------------- |
| 1    | AddressRangeMemory    | OS可用的普通内存                        |
| 2    | AddressRangeReserved  | 保留的内存                              |
| 3    | AddressRangeACPI      | ACPI 可回收内存，在OS读取完ACPI表后可用 |
| 4    | AddressRangeNVS       | ACPI NVS内存，OS不可使用                |
| 5    | AddressRangeUnusuable | 被检测到错误的内存                      |
| 6    | AddressRangeDisable   | 不可用内存                              |

在扫描时，简单起见，2、3、4 都看作保留内存，5、6 和其余未定义的值都看作不可用内存

# 物理页分配器接口

## 向下：FrameAllocator

这是为 Buddy 设计的 trait，指定了详细的参数，提供一个统一且简单的物理页分配和释放功能

```rust
pub trait FrameAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<UniqueFrames>;
    fn deallocate(&self, page: &mut Frame) -> Result<usize, FrameError>;
    /// 从 buddy 分配器中剔除指定物理内存区域（如 ioremap 需要映射的物理内存）
    ///
    /// 需要注意：`start` 须对齐到 `order`
    fn assign(&self, start: FrameNumber, order: FrameOrder) -> Result<UniqueFrames, FrameError>;
}
```

## 向上：FrameAllocOptions

由于 Rust 没有默认参数，所以我参考了标准库 `FileOpenOptions` 的设计搞了个 `FrameAllocOptions` ，并且还在最基础的页分配基础上封装了 Zone fallback（即在某个 Zone 分配失败后自动尝试另一个 Zone 分配）、重试（可选出错时直接返回、重试或者未来加入类似Linux的睡眠等待有内存可用）等功能。对功能进行了解耦合，使其更加清晰。具体用法可以参考后面的预设。

定义如下：

```rust
#[derive(Debug, Clone, Copy)]
pub struct FrameAllocOptions {
    alloc_type: FrameAllocType,
    fallback: FallbackChain,
    retry: RetryPolicy,
}
```

- `FrameAllocType` 可以选择 `Dynamic` 或是 `Fixed` （对应 `FrameAllocator::allocate` 和 `FrameAllocator::assign` ）
- `FallbackChain` 是一个数组的封装，指定了 Zone 的 fallback 顺序
- `RetryPolicy` 即重试策略，目前有 `FastFail` 和 `Retry`

### 构造参数

用于构造参数的函数如下：

```rust
impl FrameAllocOptions {
    pub const fn new() -> Self {
        Self {
            alloc_type: FrameAllocType::Dynamic {
                order: FrameOrder::new(0),
            },
            fallback: FallbackChain {
                chain: [Some(ZoneType::LinearMem), Some(ZoneType::MEM32)],
            },
            retry: RetryPolicy::FastFail,
        }
    }
    
    pub const fn retry(mut self, retry: RetryPolicy) -> Self {
        self.retry = retry;
        self
    }
    
    // 其他类似的函数略
}
```

### 预设

值得一提的是，`FrameAllocOptions` 还提供了类似 Linux `GFP_KERNEL` 和 `GFP_ATOMIC` 的预设，方便常见情况下的使用

```rust
impl FrameAllocOptions {
    /// 类似 GFP_KERNEL：通用内核分配
    ///
    /// - 优先 LinearMem，fallback 到 MEM32
    /// - 允许重试
    /// - 适用于大多数内核路径
    pub const fn kernel(order: FrameOrder) -> Self {
        Self::new().dynamic(order).retry(RetryPolicy::Retry(3))
    }

    /// 类似 GFP_ATOMIC：原子上下文分配
    ///
    /// - 优先 LinearMem，fallback 到 MEM32
    /// - 不允许重试（FailFast）
    /// - 适用于中断处理、持锁上下文等不能睡眠的场景
    pub const fn atomic(order: FrameOrder) -> Self {
        Self::new().dynamic(order).retry(RetryPolicy::FastFail)
    }
}
```

当然这里还没有 OOM Killer，所以就只是重试 3 次占个位

### 分配

真正分配的时候需要使用 `allocate` 方法：

```rust
impl FrameAllocType {
    fn allocate(&self, zone: ZoneType) -> Result<(UniqueFrames, ZoneType), FrameError> {
        match self {
            Self::Dynamic { order } => FRAME_MANAGER
                .allocate(zone, *order)
                .map(|f| (f, zone))
                .ok_or(FrameError::OutOfFrames),

            Self::Fixed { start, order } => FRAME_MANAGER.assign(*start, *order).map(|frames| {
                let paddr = PhysAddr::from_frame_number(*start);
                let zone_type = ZoneType::from_address(paddr);

                (frames, zone_type)
            }),
        }
    }
}
```

返回值是一个独占引用和这个物理页所在的 Zone 类型
