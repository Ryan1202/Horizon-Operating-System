# 内核内存分配接口规范（Spec）

> 本规范定义一套**不依赖任何动态内存分配**、高度可组合、可复用的内核内存分配抽象，用于统一管理：
>
> - 物理页分配（buddy / 预留 / 设备内存）
> - Zone fallback 与 retry 策略
> - 虚拟内存分配与映射（linear / vmalloc / ioremap）
> - SLUB / kmalloc 的底层支持

本设计的核心目标是：

- 避免在 vmalloc / buddy / SLUB 中重复实现 fallback 与重试逻辑
- 明确职责分层，在引入 reclaim / OOM 之前控制实现复杂度
- 为未来扩展 reclaim / movable / NUMA 等机制预留清晰接口空间

---

## 1. 总体分层模型

```
+---------------------------+
| Allocator (SLUB / API)    |
+---------------------------+
            |
            v
+---------------------------+
| PageAllocOptions          |  ← 虚拟页分配与映射策略
+---------------------------+
            |
            v
+---------------------------+
| FrameAllocOptions         |  ← 物理页分配与 Zone fallback
+---------------------------+
            |
            v
+---------------------------+
| Physical Memory           |  ← Buddy / Reserved / Device
+---------------------------+
```

分层原则：

- **FrameAllocOptions 只描述“从哪里、以何种方式获取物理页”**
- **PageAllocOptions 只描述“如何分配虚拟地址并建立映射”**
- 上层 allocator（如 SLUB）仅组合并使用这两类策略，而不关心其内部实现

---

## 2. 基本类型定义

### 2.1 ZoneType

```text
ZoneType:
- Linear     // 内核线性映射区（低地址）
- MEM32      // 32位可寻址物理内存
- DMA        // DMA 受限内存
```

说明：

- Zone 仅用于**物理页来源选择**
- 不隐含 reclaim、movable 或迁移语义

---

### 2.2 FrameSource

```text
FrameSource:
- Buddy      // buddy allocator
- Static     // 预留或静态物理内存（含设备内存）
```

---

## 3. FrameAllocOptions（物理页分配层）

> FrameAllocOptions 通常不被单独使用，而是作为 PageAllocOptions 或 MemCache 的子策略存在；只有在直接分配物理页（如早期内核或特殊路径）时，才会被独立使用。

### 3.1 职责

FrameAllocOptions 负责：

- 分配物理页（Frame）
- 描述 Zone fallback 顺序
- 描述 retry / fail 策略
- 描述是否需要连续物理页

不负责：

- 虚拟地址分配
- 页表映射
- 对象大小与生命周期管理

---

### 3.2 FrameAllocOptions 定义

```text
FrameAllocOptions {
    alloc_type: FrameAllocType,
    fallback: FallbackChain,
    retry: RetryPolicy,
}
```

---

### 3.3 FrameAllocType

```text
FrameAllocType {
    Dynamic {
        order: FrameOrder,
    },
    Static {
        start: FrameNumber,
        count: NonZeroUsize,
    },
}
```

- `Dynamic`：通过 buddy 动态分配物理页
- `Static`：使用预留或静态指定的物理页区间

---

### 3.4 FallbackChain

```text
FallbackChain {
    chain: [Option<ZoneType>; ZoneType::ZONE_COUNT],
}
```

语义：

- 按顺序尝试各 Zone
- 任一 Zone 成功即返回
- 不跨 Zone 自动合并物理页

示例：

```text
[Some(Linear), Some(MEM32), Some(DMA)]
```

---

### 3.5 RetryPolicy

```text
RetryPolicy:
- FailFast        // 立即失败（原子或关键路径）
- Retry(n)        // 有限次数重试
```

说明：

- 当前阶段仅保留 `FailFast` 与 `Retry(n)`
- Spin / sleep 等依赖调度与回收机制的策略将作为后续扩展

---

### 3.6 分配接口语义

```text
fn alloc_once() -> Result<(Frame, ZoneType), _>
fn allocate(f: Fn) -> Result<Frame, _>
```

- `alloc_once`：尝试分配一段连续物理内存，大小可能不足
- `allocate`：允许通过多次尝试获取所需物理页（可不连续）

---

## 4. PageAllocOptions（虚拟页分配与映射层）

> PageAllocOptions 是一个**一次性描述虚拟内存行为的策略对象**，用于指导单次页分配与映射过程，而不是长期持有的 allocator 实例。

### 4.1 职责

PageAllocOptions 负责：

- 分配虚拟页区间
- 建立虚拟页 → 物理页映射
- 描述缓存属性
- 描述虚拟地址空间类型

不负责：

- 物理页 fallback 策略
- reclaim / OOM 处理

---

### 4.2 MappingType

```text
MappingType:
- Continuous        // 物理连续映射
- Discontinuous     // 离散物理页映射（vmalloc-style）
- Identity          // VA == PA（ioremap）
```

---

### 4.3 VirtSpace

```text
VirtSpace:
- KernelLinear      // 内核线性地址区
- KernelHigh        // 内核高端或临时映射区
- UserReserved      // 预留给用户态
```

---

### 4.4 CacheAttr

```text
CacheAttr:
- Cached
- Uncached
- WriteCombine
```

---

### 4.5 PageAllocOptions 定义

```text
PageAllocOptions {
    phys: Option<FrameAllocOptions>,
    mapping: MappingType,
    cache: CacheAttr,
    vspace: VirtSpace,
}
```

语义说明：

- `phys` 字段决定**物理页获取责任归属**
  - `phys = Some`：通过 FrameAllocOptions 获取物理页
  - `phys = None`：物理地址由外部路径提供（如 ioremap）

---

在前述 FrameAllocOptions 与 PageAllocOptions 的定义基础上，下面说明**上层 allocator 如何组合并使用这些 Options**，以形成稳定、可预测的分配路径。

## 5. SLUB / kmalloc 路径约定

### 5.1 SLUB

SLUB 并非仅依赖 FrameAllocOptions，而是**同时依赖 PageAllocOptions 与 FrameAllocOptions**，二者作为一个**整体分配策略**存在于 `MemCache` 中。

关键澄清：

- SLUB 只负责对象级管理（布局、空闲链、构造/析构等）
- 物理页获取路径始终为：`PageAllocOptions → FrameAllocOptions`
- SLUB 不会绕过 PageAllocOptions 直接操作 buddy 或其他物理分配器

因此，SLUB 可以被理解为：

> 在既定 Page / Frame 分配策略之上，叠加对象复用与生命周期管理的一层机制。

```text
MemCache {   // 抽象自 kmem_cache
    object_size,
    page_policy: PageAllocOptions,
}
```

约定：

- `MemCache` 是描述分配策略的最小单位
- 所有 SLUB 分配必须绑定到一个完整的 MemCache
- 分配策略禁止在对象级别动态拼装

---

### 5.2 kmalloc

kmalloc 面向通用内核代码，需要在成功率、性能与实现复杂度之间取得平衡，其路径选择规则为：

- 小对象优先使用 SLUB，以减少碎片和管理开销
- 大对象避免高阶连续物理页分配，转而使用 vmalloc-style 映射，以降低失败率

```text
if size <= SLUB_MAX:
    SLUB
else:
    PageAllocOptions (Discontinuous mapping)
```

---

## 6. 设计约束与原则

- ❌ 不依赖任何动态内存分配
- ❌ 早期阶段不引入 reclaim / OOM
- ✔ fallback 逻辑仅存在于 FrameAlloc 层
- ✔ 虚拟映射策略与物理分配彻底解耦
- ✔ API 可组合、可预测
- ✔ 分配策略仅允许存在于**页级或 MemCache 级**

---

## 7. 与 Linux 的对应关系（摘要）

| Linux 机制      | 本规范对应                       |
| --------------- | -------------------------------- |
| buddy allocator | FrameAllocOptions                |
| GFP flags       | 显式 Options                     |
| vmalloc         | PageAllocOptions + Discontinuous |
| ioremap         | PageAllocOptions + Identity      |
| SLUB            | MemCache + PageAllocOptions      |

---

> 本规范在“可实现性”与“复杂度可控”之间采取保守取舍，适用于自研内核在 **无 reclaim / 无 OOM** 阶段使用，并为后续演进预留清晰结构空间。