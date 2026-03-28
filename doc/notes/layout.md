在进入内核之前，还是先需要了解一下x86的内存布局，做好规划

# 物理地址

## 低地址(0 - 0xfffff)

低地址基本上只在实模式下使用，其中有不少区域被划给了 BIOS ，由于碎片化而且也不大(共1MB，可用空间约638KB)，所以内核还是尽量避免使用这一块区域

| 范围            | 大小    | 用途                                                         |
| --------------- | ------- | ------------------------------------------------------------ |
| 0x00000-0x003FF | 1KB     | 中断向量表（仅实模式使用）                                   |
| 0x00400-0x004ff | 256B    | BIOS数据区                                                   |
| 0x00500-0x7BFF  | 29.75KB | 可用                                                         |
| 0x07C00-0x9FBFF | 608KB   | MBR(512字节) 被加载到0x7C00处，剩余空间均可用                |
| 0x9FC00-0x9FFFF | 1KB     | 扩展BIOS数据区                                               |
| 0xA0000-0xAFFFF | 64KB    | VGA模式的VRAM地址                                            |
| 0xB0000-0xB7FFF | 32KB    | 字符模式的缓冲区地址                                         |
| 0xC0000-0xEFFFF | 192KB   | Option ROM (显卡 BIOS 等)                                    |
| 0xF0000-0xFFFFF | 64KB    | BIOS，最后16字节为CPU启动时的默认地址，一般通过跳转指令进入BIOS |

能静态分配的内存还是尽量往这里塞，减少一点浪费，给内核需要的数据结构分配的内存如下：

- **GDT&IDT**: 共 `4KB` , `0x1000-0x1fff`
- **ARDS**: 从 `0x2000` 开始，给它预留 `4KB` ，到 `0x2fff` 结束
- **ISA DMA**: 要求有点多，基本上也不会用到了。又要限制寻址范围，又有边界限制，干脆从 `128KB (0x20000)` 开始分配 4 个 `64KB` 的缓冲区，即 `0x20000 - 0x60000`
- 内核主线程栈: 从 `0x9fc00` 开始往下，一直碰到 ISA DMA 为止，有 `255KB` ，足够大，不怕栈溢出

## 高地址

高地址中基本都可以给软件用，有一部分可能被用于 `MMIO` (Memory Mapped I/O) ，一下是几个比较常见的:

| 用途       | 起始地址   |
| ---------- | ---------- |
| Local APIC | 0xFEE00000 |
| IO APIC    | 0xFEC00000 |
| HPET       | 0xFED00000 |

不过这些地址具体是否存在占用还要取决于具体设备，当然在 `e820` 内存布局中也会把这类地址标记为保留内存

# 虚拟地址

32位 x86 的虚拟地址空间需要好好规划一下，因为虚拟地址的位宽和物理地址是一样的，所以相对比较有限

这里参考了 Linux 的布局：

- `0x00000000` 到 `0xBFFFFFFF` 这 `3GB` 内存属于用户态，内核虚拟地址是从 `0xC0000000` 开始的剩余 `1GB`
- `0xC0000000` 到 `0xEFFFFFFF` 这 `896MB` 内存属于线性映射区，固定映射到 `0x00000000` 到 `0x2FFFFFFF` 的物理地址
- `0xF0000000` 到 `0xF7FFFFFF` 这 `128MB` 用于创建非固定的映射，比如临时的非连续物理内存映射，以及映射设备驱动需要的 DMA 地址或 MMIO 地址
- `0xF8000000` 到 `0xFFFFFFFF` 全部属于页表，可以直接通过这一范围的地址修改页表

由于临时创建新的页表项可能涉及分配页表内存、修改页表、刷新 `TLB` （页表缓存），开销比较大，但同时临时的映射也是必要的，所以尽可能的把内核可以用的虚拟地址都划给了线性映射区，这样只需要在启动的时候初始化完页表就不怎么需要修改了。

# 链接脚本

由于我们需要告诉编译器/链接器使用的地址，所以还需要修改一下链接脚本：

```
/* 略 */
VIR_BASE = 0xC0000000;
KERNEL_PHY_BASE = 0x00100000;

SECTIONS {
	. = KERNEL_PHY_BASE;
	_kernel_start_phy = .;
	_kernel_start_vir = VIR_BASE + KERNEL_PHY_BASE;

    /* multiboot2位置要尽量靠前 */
    .multiboot2 ALIGN(8) : {
        KEEP(*(.multiboot2))
    } : text_phy
	
	/* 内核启动早期还没完成页表初始化 */
	/* early_init需要使用物理地址 */
	.early_init : {
		KEEP(*(.early_init))
	} : text_phy
	
	/* 接下来的段使用虚拟地址 */
	. = . + VIR_BASE;
	
	.text : AT(ADDR(.text) = VIR_BASE) {
		. = ALIGN(4)
		*(.text*)
		*(.init.text)
		*(.exit.text)
	} : text

    .rodata : AT(ADDR(.rodata) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.rodata*)
    } : data

    /* data segment */
    .data : AT(ADDR(.data) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.data)
    } : data

    /* bss segement */
    .bss : AT(ADDR(.bss) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.bss*)
    } : data

    _kernel_end_vir = .;
    _kernel_end_phy = _kernel_end_vir - VIR_BASE;
}
```

这里使用了 `AT()` 来制定段的物理地址，还定义了几个符号: `_kernel_start_vir` , `_kernel_start_phy` , `_kernel_end_vir` , `_kernel_end_phy` , `VIR_BASE` 来像内核代码传递整个内核二进制的起始及结束地址的虚拟地址和物理地址，还有内核虚拟地址的基地址