# DMA 生命周期和 FFI 边界

本文补充 `0-overview.md` 中没有展开的部分：一次 DMA 映射从创建、同步、交给设备、回收资源的完整生命周期，以及 Rust/C FFI 边界上的所有权规则。

DMA 子系统的核心实现位于 `src/kernel/memory/dma.rs` 和 `src/kernel/memory/dma/*`。C 侧公开接口定义在 `src/include/kernel/dma.h`。驱动可以持有指针和调用 helper，但不应该理解 Rust 侧结构体的内部布局。

# 概念模型

DMA 框架把一次映射拆成三个职责：

- `Source`：提供设备能访问的物理内存来源，比如 coherent 页、pool 对象、原始缓冲区或 `SoftwareIotlb` bounce buffer。
- `Mapping`：把物理地址映射成设备可见的 DMA 地址。目前 x86 使用 identity backend，但接口保留了 IOMMU 的扩展位置。
- `Sync`：在 CPU 缓冲区、bounce buffer、设备视角之间同步数据，并调用 backend 的 cache/ordering hook。

跨 FFI 后，C 侧看到的是少量 token：

- `DmaDevice *` 表示一个带约束和 backend 的 DMA 设备。
- `DmaHandle *` 表示一次 coherent、pool 或 stream 映射。
- `DmaPool *` 表示一个小对象 DMA 分配池。
- `DmaScatterList *` 是 Rust `EntryList` 的 C 视图，只能通过 helper 访问。

这些 token 的内存通常由 Rust FFI 层用 `kmalloc` 放到堆上。C 侧负责在正确时机调用对应 destroy/free/unmap 接口，但不负责直接释放 Rust 内部资源。

# 生命周期

## Device

`dma_device_create()` 根据 `DmaConstraints`、`DmaBackend` 和 coherency 标志创建 `DmaDevice`。创建后，驱动可以设置 mask、segment size、boundary 等约束。这些约束会影响后续 coherent 分配、stream 映射和 SG 映射是否需要 bounce。

`dma_device_destroy()` 会 drop Rust `Device`，再释放 `DmaDevice` 本身的堆内存。销毁 device 前，所有依赖它的 handle、pool 和正在使用的 SG 映射都必须先结束。销毁后继续使用旧指针属于 use-after-free。

## Coherent

`dma_alloc_coherent(device, size)` 创建一个 `DmaHandle`，其 `Source` 是 `Coherent`。底层会分配满足 coherent mask、boundary、contiguous 和 cache 属性要求的页，再通过 backend 得到 DMA 地址。

C 侧不能解引用 `DmaHandle`，只能使用：

- `dma_handle_cpu_addr(handle)` 获取 CPU 虚拟地址。
- `dma_handle_dma_addr(handle)` 获取设备使用的 DMA 地址。
- `dma_handle_size(handle)` 获取映射大小。

释放必须调用 `dma_free_coherent(handle)`。该接口 drop handle，触发 `Source::release()` 释放 coherent 页，然后释放 handle 本身。不要对 `dma_handle_cpu_addr()` 返回的地址直接 `kfree()`。

## Stream Single

`dma_map_single(device, buffer, length, direction)` 把一个已有 CPU 缓冲区映射给设备。它先尝试 `Direct`：要求缓冲区可翻译成物理连续地址，并满足 mask、boundary、max segment size。失败时退回 `SoftwareIotlb`，在 bounce pool 中分配设备可访问的低地址缓冲区。

映射成功后返回 `DmaHandle *`。这个 handle 是一次 stream 映射的唯一所有权 token。C 侧需要：

1. 用 `dma_handle_dma_addr()` 把 DMA 地址写入设备描述符。
2. 如果 CPU 在设备访问前继续改写缓冲区，调用 `dma_handle_sync_for_device()`。
3. 如果设备写入后 CPU 要读结果，调用 `dma_handle_sync_for_cpu()`，或在最终 `dma_unmap_single()` 时依赖 handle drop 做完整范围回同步。
4. 最后调用 `dma_unmap_single(handle)`，之后 handle 和 accessor 返回的映射语义都失效。

`dma_unmap_single()` 当前通过 drop `DmaHandle` 完成释放。drop 路径会按 direction 执行 CPU 回同步，再调用 backend `unmap()`，最后调用 `Source::release()`。因此不能绕过 `dma_unmap_single()` 去直接释放 handle 内存，否则 bounce-backed 的 `FromDevice` / `Bidirectional` 数据可能丢失。

## Pool

`dma_pool_create(name, device, size, align)` 创建一个 `DmaPool`，用于频繁分配固定大小的小 DMA 对象。pool 内部基于 `MemCache`，并使用 device 约束决定底层页分配属性。

`dma_pool_alloc(pool, out_dma)` 返回 CPU 指针，并通过 `out_dma` 返回 DMA 地址。`dma_pool_zalloc()` 额外清零对象。释放单个对象必须调用 `dma_pool_free(pool, cpu_address)`，销毁池必须调用 `dma_pool_destroy(pool)`。

pool 的调用者要保证：所有从 pool 分配的对象都已经 `dma_pool_free()`，再销毁 pool。不要把 pool 对象和 coherent handle 混用。

## Scatter-Gather

SG 映射不返回 `DmaHandle`，而是在每个 `DmaScatterList` entry 上写入映射结果。典型流程是：

1. `sg_create_segment(nents)` 分配 SG segment。
2. 对每个 entry 调用 `sg_set_buf(entry, buffer, length)`。
3. `dma_map_sg(device, sg, nents)` 返回成功映射的 entry 数量。
4. 用 `sg_dma_address(entry)` 和 `sg_dma_len(entry)` 生成设备描述符。
5. 设备完成后，按 direction 调用 `dma_sync_sg_for_cpu()` 或 `dma_sync_sg_for_device()`。
6. 调用 `dma_unmap_sg(device, sg, mapped)` 释放 DMA 映射，再释放 SG segment 内存。

当前 `dma_unmap_sg()` 只负责 backend unmap 和释放可能存在的 bounce buffer，不隐式调用 `dma_sync_sg_for_cpu()`。如果设备会写回数据，正常完成路径必须在 unmap 之前调用 `dma_sync_sg_for_cpu(device, sg, mapped, DmaFromDevice)` 或 `DmaBidirectional`。提交前失败的 cancel 路径应该只 unmap/free，不应该回拷设备并未写入的数据。

# FFI 所有权规则

FFI 层的设计目标是：C 侧拿到稳定 ABI，Rust 侧保留资源生命周期的真实实现。

| C 类型 | Rust 实体 | C 侧能做什么 | C 侧不能做什么 |
| --- | --- | --- | --- |
| `DmaDevice *` | `Device` | 创建、设置约束、销毁 | 直接访问字段、在销毁后继续给 handle/pool 使用 |
| `DmaHandle *` | `DmaHandle` | 通过 accessor 取 CPU/DMA 地址、sync、free/unmap | 复制 handle、手动 `kfree()` handle、直接释放 CPU 地址 |
| `DmaPool *` | `Pool` | 分配/释放 pool 对象、销毁 pool | 和 coherent/stream handle 混用 |
| `DmaScatterList *` | `EntryList` | 初始化 entry、读取 mapped DMA 地址和长度 | 依赖 tagged layout、把 `entry_type` 当普通指针 |

错误返回采用 C 风格：

- 返回指针的接口失败时返回 `NULL`。
- 返回数量的 SG map 失败时返回负数，成功时返回 mapped entry 数。
- sync/setter 查询类接口失败时通常返回 `-1` 或 0 值。

驱动拿到失败返回后必须立即走清理路径。特别是 map 成功之后、设备命令提交之前仍可能失败的路径，需要显式 unmap/cancel 已经建立的映射。

# Bounce Buffer 和方向语义

`SoftwareIotlb` 用低地址 bounce pool 帮不满足设备约束的缓冲区完成 DMA。它保存原始物理地址和长度，并让设备访问 bounce pool 中的地址。

direction 决定数据复制方向：

- `DmaToDevice`：CPU 数据需要在设备访问前复制到 bounce buffer。
- `DmaFromDevice`：设备写入完成后，bounce buffer 数据需要复制回 CPU 原始缓冲区。
- `DmaBidirectional`：两个方向都可能发生，需要在对应 sync 点执行复制。
- `DmaNone`：不表示有效的数据传输方向，驱动不应把它用于实际映射。

stream single 的 `DmaHandle` 在 map 后会对 `ToDevice` / `Bidirectional` 做初始 `sync_range_for_device()`。设备写入后，`sync_range_for_cpu()` 或最终 unmap/drop 会按 direction 回同步。

SG 的同步必须由调用方显式选择。`dma_sync_sg_for_device()` 只在 `ToDevice` / `Bidirectional` 时执行 bounce 前向复制；`dma_sync_sg_for_cpu()` 只在 `FromDevice` / `Bidirectional` 时执行 bounce 回拷。两者都会对已映射 entry 调用 backend 的 prepare hook。

normal completion 和 cancel 的区别很重要：

- normal completion：设备已经完成 DMA。如果 CPU 要读取设备写入的数据，先 sync for CPU，再 unmap/free。
- cancel 或 pre-submit failure：设备没有执行或不能保证执行了写入，只释放映射和 bounce 资源，不把 bounce 内容复制回原始缓冲区。

# DmaScatterList 的特殊设计

`DmaScatterList` 是 Rust `EntryList` 的 `repr(C)` 视图。它的字段能被 C 看到，但有些字段属于私有协议：

```c
typedef struct DmaScatterList {
    size_t entry_type; // private: frame/chain pointer plus internal flags
    size_t offset;     // private
    size_t length;     // private data length; use sg_dma_len()
    size_t dma_addr;   // result-only; use sg_dma_address()
} DmaScatterList;
```

`entry_type` 低位保存 `CHAIN` / `END` 标志，其余位可能是 `Frame *` 或下一个 segment 指针。驱动代码不能直接解释它，只能通过：

- `sg_create_segment()` / `sg_init_table()` 创建或初始化。
- `sg_set_buf()` 填入 CPU 缓冲区。
- `sg_next()` 遍历。
- `sg_dma_address()` / `sg_dma_len()` 读取映射后的设备地址和长度。

`dma_map_sg()` 可能把原始 entry 替换成 bounce buffer 对应的 frame、offset 和 DMA 地址。因此设备描述符必须使用 `sg_dma_address()` 的结果，不能从 CPU 虚拟地址或原始物理地址自行推导。

# 驱动侧例子

## ATA BMDMA

ATA 使用 SG API 把请求缓冲区拆成按页 entry，再把 mapped SG 输出转换成 PRDT。PRDT 生成时应使用：

- `sg_dma_address(seg)` 作为 PRD base address。
- `sg_dma_len(seg)` 作为该段设备可见长度。

`AtaDma` 状态在分配后必须清零，避免 `sg_list`、`sg_nents`、`direction` 带入未初始化值。`ata_bmdma_map_sg()` 成功后，如果后续命令提交、DRQ 检查或设备状态验证失败，必须调用 cancel/unmap 路径释放已经建立的 SG 映射和 segment。

对于读请求，如果映射可能走 bounce，并且 CPU 需要读设备写回的数据，完成路径应在 `dma_unmap_sg()` 前调用 `dma_sync_sg_for_cpu()`。取消路径不要调用 sync for CPU。

## UHCI

UHCI 使用 coherent 或 pool 资源保存控制器访问的 TD/QH 等结构。资源初始化通常是多阶段的：创建 DMA device、创建 pool、分配 skeleton、设置控制器寄存器。

失败路径要按创建顺序反向销毁。例如 `usb_create_hcd()` 或后续初始化失败时，已经创建的 TD/QH pool 需要销毁，已经分配的 coherent/pool 对象需要释放。不要只释放外层 HCD 结构，否则 Rust 侧 DMA 资源仍会留在 pool 或 handle 中。

# 常见错误

- 绕过 `dma_unmap_single()` 直接释放 handle，导致 backend unmap、bounce 回同步或 source release 没有发生。
- 对 SG 映射只调用 `dma_unmap_sg()`，却期望它自动把 `FromDevice` 数据从 bounce buffer 拷回 CPU。
- 在 map 成功后、设备真正开始 DMA 前发生错误，却走 normal completion 同步路径，把未定义 bounce 内容覆盖原始缓冲区。
- 直接读取或写入 `DmaScatterList.entry_type`，把 Rust tagged layout 当作 C ABI。
- 使用原始物理地址构建设备描述符，而不是使用 `sg_dma_address()` 或 `dma_handle_dma_addr()`。
- 销毁 `DmaDevice` 后继续使用关联的 handle、pool 或 SG 映射。
- 将 coherent、pool、stream single 的释放 API 混用。

# 维护检查点

以后修改 DMA FFI 或驱动调用点时，至少检查以下问题：

- 每个 create/map/alloc 是否有唯一对应的 destroy/unmap/free。
- `DmaDirection` 是否真的影响同步行为，而不只是被校验。
- map 成功后所有可能失败的分支是否释放映射。
- `FromDevice` / `Bidirectional` 的 normal completion 是否在 unmap/free 前完成 CPU 可见性处理。
- cancel 路径是否避免回拷设备未写入的数据。
- C 侧是否只使用公开 helper，而没有依赖 Rust 内部布局。
