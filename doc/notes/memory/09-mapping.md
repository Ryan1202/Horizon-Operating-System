我根据目前的需要，通过 trait 简单定义了一些和架构强相关的部分，这只是临时的设计，后面可能还需要大改动

# 定义

## 页表

首先是一些和分页相关的基本定义

```rust
pub trait ArchMemory {
    const PAGE_BITS: usize;
    const PAGE_SIZE: usize = 1 << Self::PAGE_BITS;

    const TOTAL_ENTRIES: usize = 1 << (Self::VIRT_ADDR_BITS - Self::PAGE_BITS);

    /// 页表层级数。
    /// - x86: 2（PD → PT）
    /// - x86_64: 4（PML4 → PDPT → PD → PT）
    /// - aarch64: 3 或 4（取决于配置）
    const PAGE_TABLE_LEVELS: usize;

    /// 虚拟地址有效位数。
    /// - x86: 32
    /// - x86_64: 48（或 57 with LA57）
    /// - aarch64: 48
    const VIRT_ADDR_BITS: usize;

    /// 物理地址有效位数。
    /// - x86: 32（或 36 with PAE）
    /// - x86_64: 52
    /// - aarch64: 48
    const PHYS_ADDR_BITS: usize;

    /// 该架构的页表条目类型
    type Entry: PageEntry;
}
```

这里的 `PageEntry` 定义了标准的页表条目操作函数。不同层级的条目属性会有一点区别，例如在 x86 的两级页表中，只有 PDE 条目支持 HugePage 选项，而且 PTE 和 PDE 的 PAT 选项所在的位不同。所以加了一个 `page_level` 参数，最低一层（如 PTE ）为 0，往上计数

```rust
/// 页表条目操作 trait
///
/// 每个架构为其 PTE 类型实现此 trait。
pub trait PageEntry: Sized {
    /// 创建一个无效的条目
    fn new_absent() -> Self;

    /// 创建一个有效条目，映射到 frame 并设置 flags
    fn new_mapped(frame: FrameNumber, flags: PageFlags, page_level: u8) -> Self;

    /// 是否有效
    fn is_present(&self) -> bool;

    /// 如果有效，返回映射的物理页号；否则返回 None
    fn frame_number(&self) -> Option<FrameNumber>;

    /// 获取当前 flags
    fn flags(&self, page_level: u8) -> PageFlags;

    /// 修改 flags（保留物理地址不变）
    fn set_flags(&mut self, flags: PageFlags);

    /// 是否是大页条目（用于多级页表中跳过下一级）
    fn is_huge(&self, page_level: u8) -> bool {
        self.flags(page_level).huge_page
    }

    /// 清空条目（设为 absent）
    fn clear(&mut self) {
        *self = Self::new_absent();
    }
}
```

以及使用 `PageTableWalker` 定义对页表的操作（如映射、取消映射、地址转换）

```rust
/// 多级页表遍历器。
///
/// 每个架构提供一个实现，负责：
/// - 将虚拟地址拆分为各级页表索引
/// - 遍历页表层级并找到目标 PTE
/// - 按需为中间级页表分配 Frame
///
/// 泛型参数 `A: ArchMemory` 提供架构常量。
pub trait PageTableWalker {
    type Entry: PageEntry;

    /// 将虚拟页映射到物理页帧
    ///
    /// 如果中间级页表不存在，使用 `frame_alloc` 分配新的页表帧。
    /// 如果目标 PTE 已经 present，返回 `EntryAlreadyMapped`。
    fn map(
        pages: &mut DynPages,
        offset: usize,
        frames: &mut UniqueFrames,
        flags: PageFlags,
    ) -> Result<(), MemoryError>;

    /// 取消虚拟页的映射
    ///
    /// 返回之前映射的物理页帧。调用者负责调用 TLB flush。
    fn unmap(pages: &mut DynPages, offset: usize, order: FrameOrder) -> Result<(), PageTableError>;

    /// 翻译虚拟地址到物理地址（不修改页表）
    fn translate(vaddr: VirtAddr) -> Result<PhysAddr, PageTableError>;

    /// 修改已有映射的 flags（不改变物理地址）
    fn update_flags(pages: &mut DynPages, flags: PageFlags) -> Result<(), PageTableError>;
}
```

在这里，物理页也是以 order 为单位被映射和接触映射的

## 地址

`PhysAddr` 和 `VirtAddr` 是由架构分别实现的，由于需要同样的操作，使用宏来 “一键” 实现。其中包括：

- 对齐和判断对齐
- 获取页内偏移
- 计算距离
- 简单的加减
- 还有输出的格式化

```rust
#[macro_export]
macro_rules! impl_page_addr {
    ($name:ident, $size:expr) => {
        impl $name {
            pub const fn is_page_aligned(&self) -> bool {
                self.0.is_multiple_of($size)
            }

            pub const fn page_align_down(self) -> Self {
                Self(self.0 & !($size - 1))
            }

            pub const fn page_align_up(self) -> Self {
                Self(self.0.next_multiple_of($size))
            }

            pub const fn page_offset(self) -> usize {
                self.0 % $size
            }

            pub const fn offset_from(self, other: Self) -> usize {
                self.0 - other.0
            }
        }

        impl core::ops::Add<usize> for $name {
            type Output = Self;

            fn add(self, rhs: usize) -> Self::Output {
                Self(self.0 + rhs)
            }
        }

        impl core::ops::AddAssign<usize> for $name {
            fn add_assign(&mut self, rhs: usize) {
                self.0 += rhs;
            }
        }

        impl core::ops::Sub<usize> for $name {
            type Output = Self;

            fn sub(self, rhs: usize) -> Self::Output {
                Self(self.0 - rhs)
            }
        }

        impl core::ops::Sub for $name {
            type Output = usize;

            fn sub(self, rhs: Self) -> Self::Output {
                self.0 - rhs.0
            }
        }

        impl core::ops::SubAssign<usize> for $name {
            fn sub_assign(&mut self, rhs: usize) {
                self.0 -= rhs;
            }
        }

        impl core::fmt::Display for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
                write!(f, "0x{:016x}", self.0)
            }
        }
    };
}
```

# 私有实现

对于 x86 来说，具体的实现如下

```rust

pub const PDE_BASE: usize = 0xFFFF_F000;
pub const PTE_BASE: usize = 0xFFC0_0000;

pub const PDE_SHIFT: usize = 10;

pub struct X86PageTable;

impl X86PageTable {
    pub fn read_pde_entry(page_number: usize) -> X86PageEntry {
        let pde_addr = (PDE_BASE + (page_number >> PDE_SHIFT) * 4) as *const AtomicU32;
        X86PageEntry(unsafe { (*pde_addr).load(Ordering::Relaxed) })
    }

    pub fn write_pde_entry(page_number: usize, entry: X86PageEntry) {
        let pde_addr = (PDE_BASE + (page_number >> PDE_SHIFT) * 4) as *mut AtomicU32;
        unsafe {
            (*pde_addr).store(entry.0, Ordering::Relaxed);
        };
    }

    pub fn read_pte_entry(page_number: usize) -> X86PageEntry {
        let pte_addr = (PTE_BASE + page_number * 4) as *const AtomicU32;
        X86PageEntry(unsafe { (*pte_addr).load(Ordering::Relaxed) })
    }

    pub fn write_pte_entry(page_number: usize, entry: X86PageEntry) {
        let pte_addr = (PTE_BASE + page_number * 4) as *mut AtomicU32;
        unsafe {
            (*pte_addr).store(entry.0, Ordering::Relaxed);
        };
    }

    fn alloc_table_frame(flags: PageFlags) -> Result<X86PageEntry, MemoryError> {
        let frame_options = FrameAllocOptions::new().fallback(&[ZoneType::LinearMem]);
        let page_options = PageAllocOptions::new(frame_options)
            .contiguous(true)
            .zeroed(true);

        let mut page = page_options.allocate()?;

        let frame = page.get_frame().unwrap();

        let pde_flags = if flags.huge_page {
            flags
        } else {
            PageFlags::new()
        };
        let new_pde = X86PageEntry::new_mapped(frame.to_frame_number(), pde_flags);

        Ok(new_pde)
    }
}
```

这些是私有函数，pde 和 pte 分别有各自独立的读写操作，以及调用内核接口分配新的页作为页表，由于是页表，使用前必须先清零。

# trait 实现

然后是对于 trait 的实现：

```rust
impl ArchMemory for X86PageTable {
    type Entry = X86PageEntry;

    const PAGE_BITS: usize = 12;
    const PAGE_TABLE_LEVELS: usize = 2;
    const PHYS_ADDR_BITS: usize = 32;
    const VIRT_ADDR_BITS: usize = 32;
}
```

IA-32 下物理地址和虚拟地址都是 32 位，一个页有 4KB，占 12 个 bit

## 页表操作的实现

这里做了大页优化：

利用了 Frame 地址必定对齐 order 的要求，只需检查 order 大于等于 10，就必定满足大页的大小要求

```rust
impl PageTableWalker for X86PageTable {
    type Entry = X86PageEntry;

    fn map(
        pages: &mut DynPages,
        offset: usize,
        frame: &mut UniqueFrames,
        flags: PageFlags,
    ) -> Result<(), MemoryError> {
        let page_number = pages.start_addr().to_page_number().unwrap().get().get() + offset;

        // 尝试使用大页
        if frame.get_order().get() >= 10 && FrameOrder::from_count(page_number).get() >= 10 {
            let pde = Self::read_pde_entry(page_number);
            if pde.is_present() {
                return Err(PageTableError::EntryAlreadyMapped.into());
            }

            Self::write_pde_entry(
                page_number,
                X86PageEntry::new_mapped(frame.to_frame_number(), flags, 1),
            );
            return Ok(());
        }

        let frame_number = frame.to_frame_number();
        for i in 0..frame.get_order().to_count().get() {
            let page_number = page_number + i;

            let pde = Self::read_pde_entry(page_number);
            if !pde.is_present() {
                // 需要分配新的页表
                let new_pde = Self::alloc_table_frame(flags)?;

                Self::write_pde_entry(page_number, new_pde);
            }

            let pte = Self::read_pte_entry(page_number);
            if pte.is_present() {
                return Err(PageTableError::EntryAlreadyMapped.into());
            }

            let new_pte = X86PageEntry::new_mapped(frame_number + i, flags, 0);
            Self::write_pte_entry(page_number, new_pte);
        }

        Ok(())
    }

    fn translate(vaddr: VirtAddr) -> Result<PhysAddr, PageTableError> {
        let page_number = vaddr.to_page_number().unwrap().get().get();

        let pde_entry = Self::read_pde_entry(page_number);
        if !pde_entry.is_present() {
            return Err(PageTableError::EntryNotPresent);
        } else if pde_entry.is_huge(1) {
            return Ok(PhysAddr::new(
                (pde_entry.0 as usize & 0xFFC0_0000) + (vaddr.as_usize() & 0x003F_FFFF),
            ));
        }

        let pte_entry = Self::read_pte_entry(page_number);
        if !pte_entry.is_present() {
            return Err(PageTableError::EntryNotPresent);
        }

        let phys_addr = PhysAddr::new((pte_entry.0 & 0xFFFF_F000) as usize | vaddr.page_offset());
        Ok(phys_addr)
    }

    fn unmap(pages: &mut DynPages, offset: usize, order: FrameOrder) -> Result<(), PageTableError> {
        let vaddr = pages.start_addr() + offset * ArchPageTable::PAGE_SIZE;
        let page_number = vaddr.to_page_number().unwrap().get().get();

        let pde = Self::read_pde_entry(page_number);
        if !pde.is_present() {
            return Err(PageTableError::EntryNotPresent);
        } else if pde.is_huge(1) {
            if order.get() >= 10 {
                let entry = X86PageEntry::new_absent();
                Self::write_pde_entry(page_number, entry);
            } else {
                return Err(PageTableError::InvalidLevel);
            }
        }

        for i in 0..order.to_count().get() {
            let pte = Self::read_pte_entry(page_number + i);
            if !pte.is_present() {
                return Err(PageTableError::EntryNotPresent);
            }

            let entry = X86PageEntry::new_absent();
            Self::write_pte_entry(page_number, entry);
        }

        Ok(())
    }

    fn update_flags(pages: &mut DynPages, flags: PageFlags) -> Result<(), PageTableError> {
        let pde = Self::read_pde_entry(pages.start_addr().to_page_number().unwrap().get().get());
        if !pde.is_present() {
            return Err(PageTableError::EntryNotPresent);
        }

        let mut pte =
            Self::read_pte_entry(pages.start_addr().to_page_number().unwrap().get().get());
        if !pte.is_present() {
            return Err(PageTableError::EntryNotPresent);
        }

        pte.set_flags(flags);
        Self::write_pte_entry(
            pages.start_addr().to_page_number().unwrap().get().get(),
            pte,
        );
        Ok(())
    }
}
```

## 页表条目的实现

基本的 flags 转换太长，而且除了一些标志位的定义需要查英特尔手册，也没什么好说的就不放了

```rust
#[repr(transparent)]
pub struct X86PageEntry(pub(super) u32);

impl PageEntry for X86PageEntry {
    fn new_absent() -> Self {
        X86PageEntry(0)
    }

    fn new_mapped(frame: FrameNumber, flags: PageFlags, page_level: u8) -> Self {
        let mut entry = Self((frame.get() * ArchPageTable::PAGE_SIZE) as u32);

        let flags = if page_level == 0 {
            flags.huge_page(false)
        } else {
            flags
        };
        entry.set_flags(flags);
        entry
    }

    fn is_present(&self) -> bool {
        (self.0 & 0x1) == 1
    }

    fn frame_number(&self) -> Option<FrameNumber> {
        if self.is_present() {
            Some(FrameNumber::new(self.0 as usize / ArchPageTable::PAGE_SIZE))
        } else {
            None
        }
    }

    fn flags(&self, page_level: u8) -> PageFlags {
        X86PageFlags(self.0 & 0xFFF).as_page_flags(page_level == 0)
    }

    fn set_flags(&mut self, flags: PageFlags) {
        let flags = X86PageFlags::from(flags);
        self.0 = (self.0 & !0xFFF) | flags.0;
    }
}

```

# 动态映射

目前的设计，内核的所有动态映射都通过 `DynPages` 描述，前面有提过一次结构，再放一遍：

```rust
pub struct DynPages {
    pub(super) rb_node: LinkedRbNodeBase<VmRange, usize>,
    pub(super) frame_count: usize,
    pub(super) head_frame: Option<UniqueFrames>,
}
```

先是一些基础的工具函数

```rust
impl DynPages {
    const fn new(range: VmRange) -> Self {
        let count = range.get_count();
        DynPages {
            rb_node: LinkedRbNodeBase::linked_new(range, count),
            frame_count: 0,
            head_frame: None,
        }
    }

    /// 获取内核可用临时虚拟地址空间的范围
    #[inline(always)]
    pub fn kernel() -> Self {
        let start = addr_of!(VIR_BASE) as usize + KLINEAR_SIZE;
        let (start, end) = (VirtAddr::new(start), KMEMORY_END);

        let start = start.to_page_number().unwrap();
        let end = end.to_page_number().unwrap();

        let vm_range = VmRange { start, end };

        Self::new(vm_range)
    }

    pub const fn start_addr(&self) -> VirtAddr {
        let addr = self.rb_node.get_key().start.get().get() * ArchPageTable::PAGE_SIZE;
        VirtAddr::new(addr)
    }
}
```

其中还有个 `split` 前面也讲过就不放出来了

## 映射

本来传入的就是一组 Frame，原样传递给映射函数就好了

```rust
pub fn map<W: PageTableWalker>(
    &mut self,
    mut frame: UniqueFrames,
    cache_type: PageCacheType,
) -> Result<(), MemoryError> {
    // 由于vmap只使用range.start做比较，所以修改end不会影响树结构
    if !matches!(frame.get_tag(), FrameTag::Anonymous) {
        return Err(MemoryError::UnavailableFrame);
    };
    let count = frame.get_order().to_count().get();

    unsafe { frame.get_data_mut().anonymous.acquire() };

    W::map(
        self,
        self.frame_count,
        &mut frame,
        PageFlags::new().cache_type(cache_type),
    )
    .inspect_err(|_| unsafe {
        frame.get_data().anonymous.release();
    })?;
    {
        let range = self.rb_node.get_key();

        if self.head_frame.is_none() {
            self.head_frame = Some(frame);
        } else {
            mem::forget(frame);
        }

        if self.frame_count + count > range.get_count() {
            printk!(
                "WARNING: DynPages range insufficient: required {}, available {}",
                self.frame_count + count,
                range.get_count()
            );
        }
    }

    self.frame_count += count;

    Ok(())
}
```

## 解除映射

这里存在两种情况：

- 如果被映射的物理页 order 不为 0，那么后面至少会有一个为 `Tail` 的页，读取其中的 `range` 就可以得知 Frame 的 order 了
- 如果被映射的物理页 order 为 0，那么后一个页肯定不会是 `Tail`

```rust
pub fn unmap<W: PageTableWalker>(&mut self) -> Result<(), PageTableError> {
    let mut page_number = self.start_addr().to_page_number().unwrap();
    let mut offset = 0;

    while offset < self.frame_count {
        let vaddr = page_number.to_addr();
        let paddr = W::translate(vaddr).unwrap();
        let frame_number = paddr.to_frame_number();
        let tag = Frame::get_tag_relaxed(frame_number + 1);

        let (next, order) = if let FrameTag::Tail = tag {
            let frame = unsafe { Frame::get_raw(frame_number + 1).as_ref() };

            let range = unsafe { frame.get_data().range };
            let count = range.end.count_from(range.start);

            let next = page_number + count;
            let order = FrameOrder::from_count(count);

            (next, order)
        } else {
            (page_number + 1, FrameOrder::new(0))
        };
        page_number = next;

        let _ = W::unmap(self, offset, order).inspect_err(|e| {
            printk!(
                "unmap range failed! error: {:?}, start: {}, offset: {}, order: {:?}\n",
                e,
                self.start_addr(),
                offset,
                order
            )
        });

        // 最后释放
        unsafe {
            try_free(Frame::get_raw(frame_number));
        }
        offset += order.to_count().get();
    }

    self.frame_count = 0;
    Ok(())
}
```

