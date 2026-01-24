# 内核内存分配接口规范（Spec）

> 本规范定义一套**不依赖任何动态内存分配**、可高度组合、可复用的内核内存分配抽象，用于统一管理：
>
> - 物理页分配（buddy / 预留 / 设备）
> - Zone fallback / retry 策略
> - 虚拟内存分配与映射（linear / vmalloc / ioremap）
> - SLUB / kmalloc 的底层支持

目标是：

- 避免在 vmalloc / buddy / slub 中重复实现 fallback 逻辑
- 明确职责分层，降低 reclaim / OOM 引入前的复杂度
- 为未来扩展 reclaim / movable / NUMA 留接口空间

------

## 1. 总体分层模型

```
+---------------------------+
| Allocator (SLUB / API)    |
+---------------------------+
            |
            v
+---------------------------+
| PageAllocOptions          |  ← 虚拟页分配 + 映射
+---------------------------+
            |
            v
+---------------------------+
| FrameAllocOptions         |  ← 物理页分配 + Zone fallback
+---------------------------+
            |
            v
+---------------------------+
| Physical Memory (Buddy / Reserved)
+---------------------------+
```

原则：

- **FrameAllocOptions 只关心“从哪里拿物理页”**
- **PageAllocOptions 只关心“如何建立虚拟映射”**
- SLUB 通过 PageAllocOptions 分配可用的页，来提供小内存的分配功能

------

## 2. 基本类型定义

### 2.1 ZoneType

```text
ZoneType:
- Linear     // 内核线性映射区（低地址）
- HighMem    // 高端物理内存
- DMA        // DMA 受限内存
```

Zone 只用于**物理页来源选择**，不隐含 reclaim / movable 语义。

------

### 2.2 FrameSource

```text
FrameSource:
- Buddy      // buddy allocator
- Static     // 预留/静态物理内存
- Device     // 设备指定物理地址（MMIO 等）
```

------

## 3. FrameAllocOptions（物理页分配层）

> FrameAllocOptions 通常不被单独使用，而是作为 PageAllocOptions 的子策略存在；只有在直接分配页（如早期内核或特殊路径）时，才会被独立使用。

### 3.1 职责

FrameAllocOptions 负责：

- 分配**物理页（Frame）**
- 管理 Zone fallback 顺序
- 管理 retry / fail 策略
- 管理 contiguous 需求

**不负责：**

- 虚拟地址
- 页表映射
- 对象大小管理

------

### 3.2 FrameAllocOptions 定义

```text
FrameAllocOptions {
    source: FrameSource
    zones: ZoneChain
    pages: usize
    contiguous: bool
    zeroed: bool
    retry: RetryPolicy
}
```

------

### 3.3 ZoneChain（Zone fallback）

```text
ZoneChain:
- [ZoneType]
```

语义：

- 按顺序尝试各 Zone
- 任一 Zone 成功即返回
- 不自动跨 Zone 合并

示例：

```text
[Linear, HighMem, DMA]
```

------

### 3.4 contiguous 语义

```text
contiguous = true
```

表示：

- **调用者语义上要求物理连续**
- FrameAlloc 内部可使用：
  - buddy order
  - CMA
  - 其他实现手段

调用者**不感知 order**。

------

### 3.5 RetryPolicy

```text
RetryPolicy:
- FailFast        // 立即失败（原子路径）
- Retry(n)        // 有限次数重试
```

> 根据当前设计约束，RetryPolicy 仅保留 FailFast 与 Retry(n) 两种策略。 Spin / sleep 等依赖调度或回收机制的策略被视为后续扩展内容，待引入完整的调度与内存回收体系后再行支持。

------

### 3.6 返回值

```text
FrameAllocResult:
- Single(Frame)
- List([Frame])
```

由 contiguous 决定返回形式。

------

## 4. PageAllocOptions（虚拟页分配与映射层）

### 4.1 职责

PageAllocOptions 负责：

- 分配虚拟页区间
- 建立虚拟页 → 物理页映射
- 管理缓存属性
- 管理虚拟地址空间类型

**不负责：**

- 物理页 fallback
- reclaim / OOM

------

### 4.2 MappingType

```text
MappingType:
- Continuous        // 物理连续映射
- Discontinuous     // 离散物理页映射（vmalloc）
- Identity          // VA == PA（ioremap）
```

------

### 4.3 VirtSpace

```text
VirtSpace:
- KernelLinear      // 内核线性地址区
- KernelHigh        // 内核高端/临时映射
- UserReserved      // 预留给用户态
```

------

### 4.4 CacheAttr

```text
CacheAttr:
- Cached
- Uncached
- WriteCombine
```

------

### 4.5 PageAllocOptions 定义

```text
PageAllocOptions {
    phys: Option<FrameAllocOptions>
    mapping: MappingType
    cache: CacheAttr
    vspace: VirtSpace
}
```

语义：

- phys = None → 物理地址已知（ioremap）
- phys = Some → 通过 FrameAllocOptions 获取物理页

------

## 5. SLUB / kmalloc 路径约定

### 5.1 SLUB

- 每个 MemCache 绑定一个 PageAllocOptions
- 对象大小由 SLUB 管理

```text
MemCache {
    object_size
    policy: PageAllocOptions
}
```

------

### 5.2 kmalloc

```text
if size <= SLUB_MAX:
    SLUB
else:
    PageAllocOptions (vmalloc-style)
```

------

## 6. ioremap 约定

```text
PageAllocOptions {
    phys: None
    mapping: Identity
    cache: Uncached
    vspace: KernelHigh
}
```

------

## 7. 设计约束与原则

- ❌ 不依赖任何动态内存分配
- ❌ 不在早期引入 reclaim / OOM
- ✔ fallback 逻辑只存在于 FrameAlloc 层
- ✔ 映射策略与物理分配彻底解耦
- ✔ API 可组合、可链式构造
- ✔ 分配策略只能以“页级或 MemCache 级”存在，禁止在对象级别动态拼装策略，以确保实现简单性和可预测性

------

## 8. 与 Linux 的对应关系（摘要）

| Linux           | 本 Spec                          |
| --------------- | -------------------------------- |
| buddy allocator | FrameAllocOptions                |
| GFP flags       | 显式 Options                     |
| vmalloc         | PageAllocOptions + Discontinuous |
| ioremap         | PageAllocOptions + Identity      |
| SLUB            | kmem_cache + FrameAllocOptions   |

------

> 本规范刻意在“可实现性”和“复杂度可控”之间取保守平衡，适合自研内核在 **无 reclaim / 无 OOM killer** 阶段使用，并为后续扩展预留结构空间。