# C 接口速查表

本文档提供所有导出给 C 语言的内存管理接口的快速参考。

---

## 1. 分配函数

| 函数 | 头文件 | 返回值 | 说明 |
|------|--------|--------|------|
| `kmalloc(size)` | `memory.h` | `void*` | 分配小对象，失败返回 NULL |
| `kzalloc(size)` | `memory.h` | `void*` | 分配并清零，失败返回 NULL |
| `vmalloc(size, cache_type)` | `memory.h` | `void*` | 虚拟连续分配，失败返回 NULL |
| `ioremap(addr, size, cache_type)` | `memory.h` | `void*` | 映射物理地址，失败返回 NULL |
| `kmalloc_pages(count)` | `memory.h` | `void*` | 分配物理页，失败返回 NULL |
| `mem_cache_create(name, size, align)` | `slab.h` | `struct mem_cache*` | 创建对象缓存，失败返回 NULL |

### 1.1 分配示例

```c
#include "memory.h"
#include "slab.h"

// 分配 128 字节
void *buf = kmalloc(128);
if (buf == NULL) {
    return -1;
}

// 分配 64KB vmalloc
void *vbuf = vmalloc(65536, 0);  // cache_type = WriteBack
if (vbuf == NULL) {
    return -1;
}

// 分配 16 页
void *pages = kmalloc_pages(16);
if (pages == NULL) {
    return -1;
}

// 创建自定义缓存
struct mem_cache *cache = mem_cache_create("my_cache", 64, 8);
if (cache == NULL) {
    return -1;
}
```

---

## 2. 释放函数

| 函数 | 头文件 | 返回值 | 说明 |
|------|--------|--------|------|
| `kfree(ptr)` | `memory.h` | `void` | 无返回值 |
| `vfree(ptr)` | `memory.h` | - | **无 C 接口**，需从 Rust 调用 |
| `iounmap(vaddr)` | `memory.h` | `int` | 0=成功, -1=失败 |
| `kfree_pages(vaddr)` | `memory.h` | `int` | 0=成功, -1=失败 |
| `mem_cache_destroy(cache)` | `slab.h` | `int` | 0=成功, -1=失败 |

> 注意：`vfree` 没有导出 C 接口，如果需要释放 vmalloc 分配的内存，请在 Rust 代码中调用 `vfree`。

### 2.1 释放示例

```c
// 释放 kmalloc 内存（无返回值）
kfree(buf);

// 使用完 iounmap 后解除映射
iounmap(vaddr);

// 释放页
kfree_pages(pages);

// 释放对象缓存
mem_cache_destroy(cache);
```

---

## 3. 缓存类型

使用整数值指定缓存类型（枚举直接映射为整数）：

| 值 | 说明 |
|-----|------|
| `0` | WriteBack - 写回缓存 |
| `1` | WriteCombine - 合并写 |
| `2` | WriteThrough - 直写 |
| `3` | Uncached - 不缓存 |
| `4` | UncachedMinus - 弱缓存 |

### 3.1 使用示例

```c
// 映射设备寄存器（需要 Uncached）
void *regs = ioremap(0xFED00000, 4096, 3);  // 3 = Uncached

// vmalloc 使用 WriteBack
void *buf = vmalloc(65536, 0);  // 0 = WriteBack
```

---

## 4. 头文件引用

```c
// 基础分配
#include "memory.h"

// 对象缓存（SLUB）
#include "slab.h"
```

---

## 5. 快速查找表

### 5.1 按大小选择

| 需求大小 | 推荐函数 | 备注 |
|----------|----------|------|
| ≤ 4KB | `kmalloc` | 使用 SLUB 缓存 |
| > 4KB，≤ 4MB | `kmalloc_pages` | 按页分配 |
| 任意，大块 | `vmalloc` | 物理不连续 |
| 固定设备地址 | `ioremap` | 映射 MMIO |

### 5.2 按释放方式

| 分配函数 | 对应释放函数 |
|----------|--------------|
| `kmalloc` | `kfree` |
| `kzalloc` | `kfree`（用 `kfree` 效果相同） |
| `vmalloc` | `vfree` |
| `ioremap` | `iounmap` |
| `kmalloc_pages` | `kfree_pages` |
| `mem_cache_alloc` | `kfree` |

---

## 6. 错误返回值

| 函数类型 | 错误返回值 | 含义 |
|----------|------------|------|
| 分配函数 | `NULL` | 内存不足或参数无效 |
| 释放函数 | `-1` | 无效指针或双倍释放 |
| `mem_cache_destroy` | `-1` | 缓存销毁失败 |

### 6.1 错误检查

```c
void *ptr = kmalloc(size);
if (ptr == NULL) {
    printk("ERROR: kmalloc failed\n");
    return -1;
}

if (kfree(ptr) != 0) {
    printk("ERROR: kfree failed\n");
    return -1;
}
```

---

## 7. 相关文档

- [01-overview.md](./01-overview.md) - 内存管理总览
- [02-kmalloc.md](./02-kmalloc.md) - 小内存分配
- [03-pages.md](./03-pages.md) - 页级分配
- [04-vmalloc.md](./04-vmalloc.md) - 虚拟内存分配
- [05-memcache.md](./05-memcache.md) - 对象缓存
- [07-errors.md](./07-errors.md) - 错误处理