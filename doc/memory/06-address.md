# 地址类型与转换

Horizon 内核使用强类型来区分不同类型的地址，减少操作错误。

---

## 1. 地址类型概述

| 类型 | 用途 | 底层表示 |
|------|------|----------|
| `PhysAddr` | 物理地址 | `usize` |
| `VirtAddr` | 虚拟地址 | `usize` |
| `FrameNumber` | 物理页号 | `usize` |
| `PageNumber` | 虚拟页号 | `NonZeroUsize` |

---

## 2. 类型定义

### 2.1 PhysAddr - 物理地址

```rust
pub struct PhysAddr(usize);

impl PhysAddr {
    // 创建物理地址
    pub const fn new(addr: usize) -> Self;

    // 获取原始 usize 值
    pub const fn as_usize(self) -> usize;

    // 转换为物理页号
    pub fn to_frame_number(self) -> FrameNumber;

    // 页对齐操作
    pub const fn page_align_down(self) -> Self;
    pub const fn page_align_up(self) -> Self;
    pub const fn page_offset(self) -> usize;
}
```

### 2.2 VirtAddr - 虚拟地址

```rust
pub struct VirtAddr(usize);

impl VirtAddr {
    // 创建虚拟地址
    pub const fn new(addr: usize) -> Self;

    // 获取原始 usize 值
    pub const fn as_usize(self) -> usize;

    // 转换为虚拟页号
    pub fn to_page_number(self) -> Option<PageNumber>;

    // 页对齐操作
    pub const fn page_align_down(self) -> Self;
    pub const fn page_align_up(self) -> Self;
    pub const fn page_offset(self) -> usize;
    pub const fn is_page_aligned(self) -> bool;
}
```

### 2.3 FrameNumber - 物理页号

```rust
pub struct FrameNumber(usize);

impl FrameNumber {
    pub const fn new(num: usize) -> Self;
    pub const fn get(self) -> usize;

    // 物理页号 → 物理地址
    pub fn to_phys_addr(self) -> PhysAddr;

    // 向下对齐到指定 order
    pub const fn align_down(self, order: FrameOrder) -> Self;
}
```

### 2.4 PageNumber - 虚拟页号

```rust
// 注意：使用 NonZeroUsize，0 表示无效
pub struct PageNumber(NonZeroUsize);

impl PageNumber {
    pub const fn new(num: NonZeroUsize) -> Self;
    pub const fn get(self) -> NonZeroUsize;

    // 虚拟页号 → 虚拟地址
    pub const fn to_addr(self) -> VirtAddr;
}
```

---

## 3. 地址转换关系

```mermaid
flowchart TD
    subgraph "物理侧"
        PA["PhysAddr"]
        FN["FrameNumber"]
    end

    subgraph "虚拟侧"
        VA["VirtAddr"]
        PN["PageNumber"]
    end

    PA -->|"to_frame_number()"| FN
    FN -->|"to_phys_addr()"| PA

    VA -->|"to_page_number()"| PN
    PN -->|"to_virt_addr()"| VA
```

---

## 4. 常见转换场景

### 4.1 线性映射区转换

在线性映射区，虚拟地址和物理地址有固定偏移关系：

```mermaid
flowchart LR
    subgraph "线性映射"
        PHY["物理地址<br/>x"]
        VIR["虚拟地址<br/>0xC0000000 + x"]
    end
    PHY -.->|加上偏移| VIR
    VIR -.->|减去偏移| PHY
```

```rust
let vaddr = VirtAddr::new(0xC1000000);
let offset = vaddr.offset_from(vir_base_addr());  // 偏移量
let paddr = PhysAddr::new(offset);  // 物理地址
```

### 4.2 虚拟地址转换到物理地址（页表查询）

```mermaid
sequenceDiagram
    participant User as 代码
    participant PT as PageTableWalker
    participant Page as 页表

    User->>PT: translate(vaddr)
    PT->>Page: 读取 PDE/PTE
    alt 存在有效映射
        Page->>PT: 返回物理地址
        PT->>User: 返回 PhysAddr
    else 不存在映射
        Page->>PT: 返回错误
        PT->>User: PageTableError::EntryNotPresent
    end
```

```rust
use crate::kernel::memory::mapping::PageTableWalker;
use crate::kernel::memory::arch::X86PageTable;

// 查询虚拟地址对应的物理地址
let vaddr = VirtAddr::new(some_address);
match X86PageTable::translate(vaddr) {
    Ok(paddr) => println!("物理地址: {}", paddr),
    Err(e) => println!("翻译失败: {:?}", e),
}
```

### 4.3 物理地址转虚拟地址（临时映射）

```mermaid
flowchart TD
    P["PhysAddr<br/>物理地址"]
    V["VirtAddr<br/>临时映射区"]
    
    P -->|"建立页表映射"| V1["分配虚拟地址<br/>在临时映射区"]
    V1 -->|"返回"| V
```

这种方法主要用于 ioremap 场景，详情见 [04-vmalloc.md](./04-vmalloc.md)。

---

## 5. 常用操作示例

### 5.1 地址对齐检查

```rust
let vaddr = VirtAddr::new(0xC1000123);

// 检查是否页对齐
if !vaddr.is_page_aligned() {
    println!("地址未对齐，偏移: {:#x}", vaddr.page_offset());
}

// 对齐到页边界
let aligned = vaddr.page_align_down();
let next_page = vaddr.page_align_up();
```

### 5.2 页号与地址互转

```rust
use crate::kernel::memory::frame::FrameNumber;
use crate::kernel::memory::arch::ArchPageTable;

// 物理页号 → 物理地址
let frame = FrameNumber::new(256);
let paddr = frame.to_phys_addr();

// 物理地址 → 物理页号
let paddr = PhysAddr::new(0x100000);
let frame = paddr.to_frame_number();

// FrameNumber 的基本操作
let frame_val = frame.get();
```

### 5.3 获取内核线性基地址

```rust
use crate::kernel::memory::vir_base_addr;

// 获取内核线性映射区的起始虚拟地址
let base = vir_base_addr();
println!("内核线性基地址: {:#x}", base);
```

---

## 6. Zone 判断

根据地址判断所属 Zone：

```mermaid
flowchart TD
    Addr["地址"] --> Check{地址值}

    Check -->|"< 0x100000"| Reserved["保留区"]
    Check -->|"0x100000 - 0x30000000"| LinearMem["线性映射区"]
    Check -->|">= 0x30000000"| MEM32["临时映射区"]
```

```rust
use crate::kernel::memory::frame::ZoneType;

let paddr = PhysAddr::new(0x20000000);
let zone = ZoneType::from_address(paddr);
match zone {
    ZoneType::LinearMem => println!("在线性映射区"),
    ZoneType::MEM32 => println!("在临时映射区"),
}
```

---

## 7. 相关文档

- [01-overview.md](./01-overview.md) - 内存管理总览
- [02-kmalloc.md](./02-kmalloc.md) - 小内存分配
- [04-vmalloc.md](./04-vmalloc.md) - 虚拟内存分配
- [07-errors.md](./07-errors.md) - 错误处理