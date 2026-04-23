是时候丢弃老旧的32位保护模式，进入现代的64位模式了！

# 流程

根据 英特尔手册3A卷 *Initializing IA-32e Mode* 章节的介绍，从32位保护模式切换到 IA-32e 模式的流程如下：

1. 使用 `MOV CR0` 指令设置 `CR0.PG` 为 0，关闭分页
2. 通过设置 `CR4.PAE` 为 1，启用物理地址扩展（PAE），如果失败会触发 #GP 异常
3. 将 `CR3` 设为 4 级页表（PML4）或 5 级页表（PML5）的基地址
4. 通过设置 MSR 寄存器 `IA32_EFER.LME` 为 1，启用 IA-32e 功能
5. 设置 `CR0.PG` 启用分页，此时处理器会自动设置 `IA32_EFER.LMA` 为 1，进入 IA-32e 模式

其中页表结构地址必须在物理地址的低 4GB，因为此时的 CR3 只有低 32 位有效，进入 IA-32e 模式之后才可以重新将其设为 64 位下的有效物理地址

另外，在进入 IA-32e 模式后，需要重新设置 GDT, LDT, IDT, TR 来使分段管理能够使用 64 位地址

# 模式

英特尔在 32 位时代的架构叫 Intel Architecture 32 ( IA-32 )，而在被迫跟随 AMD 推出 64 位处理器后的架构叫做 Intel 64，而不是 IA-64，因为 IA-64 是英特尔的另一个架构（放弃 x86 兼容性，使用超长指令字等等），在 Intel 64 中加入的支持 64 位的模式叫 IA-32e。

我们一般说的 32 位模式指的是 32 位保护模式，使用 32 位操作数的同时启用了保护模式，分页则是可选项。而英特尔又定义了 IA-32e 模式，其中包含两个子模式：

- 兼容模式：使用 32 位段寄存器，支持执行 32 位保护模式程序
- 64 位模式：分段被基本（但并不是完全）禁用，CS, DS, ES, SS 都被设为 0

# 分页

64 位下的分页和 32 位的分页有些许不同：

- 32 位模式下使用 2 级页表，支持 4MB 大页，可以通过启用 PAE 来启用 3 级页表以支持 52 位物理地址（36 位有效）
- 64 位模式下使用 4 级或者 5 级页表，支持 2MB 或 1GB 大页
  - 4 级页表：线性地址 48 位，物理地址 52 位
  - 5 级页表：线性地址 57 位，物理地址 52 位

  这里就不管 PAE 模式的三级页表，直接介绍 64 位模式的页表了

  64 位下，最小的页大小仍然是 4 KB，但是由于位宽扩展到了 64 位，一个页的条目个数从 1024 个减少到了 512 个，在线性地址中一级页表占 9 位

  每一级页表都有一个名字，从最低到最高是：

- Page Table（PT）：页表
- Page Directory（PDT）：页目录表
- Page Directory Pointer Table（PDPT）：硬要翻译的话应该是页目录指针表
- Page Map Level 4（PML4）
- Page Map Level 5（PML5）

第四和第五级页表的缩写一下子就清秀了起来，前面的都是什么牛鬼蛇神

## CR3

有一个 PCID ( process-context identifiers ) 功能，用于切换进程时保留部分页表有效（如内核页表）减少性能损失，在启用时 CR3 的定义会发生变化

### 关闭 PCID 时

| 位       | 内容                           |
| -------- | ------------------------------ |
| 2:0      | 忽略                           |
| 3（PWT） | 页级写穿透                     |
| 4（PCD） | 页级缓存禁用                   |
| 11:5     | 忽略                           |
| M-1:12   | 4KB 对齐的最顶层页表物理基地址 |
| 60:M     | 保留，必须为0                  |
| 61       | 启用 LAM57                     |
| 62       | 启用 LAM48，在 61 位设置时忽略 |
| 63       | 保留，必须为0                  |

### 启用 PCID 时

| 位     | 内容                           |
| ------ | ------------------------------ |
| 11:0   | PCID                           |
| M-1:12 | 4KB 对齐的最顶层页表物理基地址 |
| 60:M   | 保留，必须为0                  |
| 61     | 启用 LAM57                     |
| 62     | 启用 LAM48，在 61 位设置时忽略 |
| 63     | 保留，必须为0                  |

## 页表条目

**PML5 / PML4**

| 63   | 62:52 | 51:M | M-1:12   | 11   | 10:8 | 7    | 6    | 5    | 4    | 3    | 2    | 1    | 0    |
| ---- | ----- | ---- | -------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| XD   | 忽略  | 0    | 物理地址 | R    | 忽略 | 0    | 忽略 | A    | PCD  | PWT  | U/S  | R/W  | P    |

**PDPT / PDT**

| 63   | 62:52 | 51:M | M-1:12   | 11   | 10:8 | 7    | 6    | 5    | 4    | 3    | 2    | 1    | 0    |
| ---- | ----- | ---- | -------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| XD   | 忽略  | 0    | 物理地址 | R    | 忽略 | PS   | 忽略 | A    | PCD  | PWT  | U/S  | R/W  | P    |

**PT**

| 63   | 62:59     | 58:52 | 51:M | M-1:12   | 11   | 10:9 | 8    | 7    | 6    | 5    | 4    | 3    | 2    | 1    | 0    |
| ---- | --------- | ----- | ---- | -------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| XD   | Prot. key | 忽略  | 0    | 物理地址 | R    | 忽略 | G    | PAT  | D    | A    | PCD  | PWT  | U/S  | R/W  | P    |

其中，如果启用了大页，PDPT / PDT 的条目定义会发生变化

**PDPT 1GB 大页 / PDT 2MB 大页**

| 63   | 62:59     | 58:52 | 51:M | M-1:13   | 12   | 11   | 10:9 | 8    | 7    | 6    | 5    | 4    | 3    | 2    | 1    | 0    |
| ---- | --------- | ----- | ---- | -------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| XD   | Prot. key | 忽略  | 0    | 物理地址 | PAT  | R    | 忽略 | G    | PS   | D    | A    | PCD  | PWT  | U/S  | R/W  | P    |

可以发现，启用了大页之后，PDT 和 PDPT 条目的结构变得和 PT 条目非常像，因为他们要承担和 PT 条目相同的功能。另外，两个大页类型的物理地址也需要和大页的大小对齐

下面是各个字段的解释

| 字段      | 解释                                                         |
| --------- | ------------------------------------------------------------ |
| P         | 页存在                                                       |
| R/W       | 1表示可写                                                    |
| U/S       | 1表示用户态可用                                              |
| PWT       | 缓存写穿透                                                   |
| PCD       | 缓存禁用                                                     |
| A         | 软件访问过该页                                               |
| D         | 该页被修改过                                                 |
| PS        | 大页标志位，设置 1 启用                                      |
| G         | 全局页，CR4.PGE = 1 时可用                                   |
| R         | 正常的分页用不到，先不管                                     |
| PAT       | 为 1 时，与 PCD 和 PWT 组合表示可以自定义的缓存策略          |
| Prot. key | Protection key；CR4.PKE 或 CR4.PKS 为 1 时，可以控制页的访问权限 |
| XD        | 当 IA32_EFER.NXE = 1 时，控制执行权限（为 1 时禁止将该页的数据作为指令执行 |

# 段描述符

| 63:56       | 55   | 54   | 53   | 52   | 51:48     | 47   | 46:45 | 44   | 43:40 | 39:16      | 15:0     |
| ----------- | ---- | ---- | ---- | ---- | --------- | ---- | ----- | ---- | ----- | ---------- | -------- |
| 基地址31:24 | G    | D/B  | L    | AVL  | 大小19:16 | P    | DPL   | S    | Type  | 基地址23:0 | 大小15:0 |

| 字段 | 解释                                  |
| ---- | ------------------------------------- |
| L    | 64 位代码段，该为置 1 时，D/B必须置 0 |
| AVL  | 可以给操作系统随意修改的一位          |
| D/B  | 0 = 16 位段，1 = 32位段               |
| DPL  | 描述符特权等级                        |
| G    | 为 1 时，大小被放大 4096 倍           |
| P    | 存在                                  |
| S    | 0 = 系统段，1 = 代码或数据段          |

# 门描述符

不同于段描述符，门描述符在 64 位模式下长度发生了变化，从 8 字节变成了 16 字节，这里主要介绍 IDT 描述符和 TSS

## 中断 / 陷阱门

高 8 字节

| 63:32 | 31:0      |
| ----- | --------- |
| 保留  | 偏移63:32 |

低 8 字节

| 63:48     | 47   | 46:45 | 44   | 43:40 | 39:37 | 36   | 35   | 34:32 | 31:16    | 15:0     |
| --------- | ---- | ----- | ---- | ----- | ----- | ---- | ---- | ----- | -------- | -------- |
| 偏移31:16 | P    | DPL   | 0    | Type  | 0 0 0 | 0    | 0    | IST   | 段选择子 | 偏移15:0 |

## TSS

| 偏移 | 大小 | 内容         |
| ---- | ---- | ------------ |
| 0    | 4    | 保留         |
| 4    | 8    | RSP0         |
| 12   | 8    | RSP1         |
| 20   | 8    | RSP2         |
| 28   | 8    | 保留         |
| 36   | 8    | IST1         |
| 44   | 8    | IST2         |
| 52   | 8    | IST3         |
| 60   | 8    | IST4         |
| 68   | 8    | IST5         |
| 76   | 8    | IST6         |
| 84   | 8    | IST7         |
| 92   | 10   | 保留         |
| 102  | 2    | I/O Map 地址 |

# 实现

这里由于从 multiboot2 引导进入内核程序的时候已经进入了 32 位保护模式，有了临时GDT并关闭了分页，所以要做的事不算多



进入 `start` 时，需要先保存 multiboot2 需要检查的 eax 和 ebx，这里直接保存到 edi 和 esi，因为在 64 位下 C 使用 System V ABI 调用约定，前两个函数参数使用 rdi 和 rsi 来传递

```assembly
start:
    cli
	mov edi, eax
	mov esi, ebx
```

## 启用 PAE

```assembly
	mov eax, cr4
	bts eax, 5		; 启用 PAE
	mov cr4, eax
```

## 配置分页

在开启分页前，也需要配置好 64 位下使用的 4 级页表。之前对前 1 MB 的内存规划还空了不少部分，直接拿来作页表。

`0x1000` 和 `0x2000` 开始的页被用来放 GDT, IDT 和 e820 布局了，这里取 `0x3000` 和 `0x4000` 存放 PML4 以及第 0 个 PDPT，这里直接使用 2 MB 大页，可以省去很多麻烦，只要保证配置正式配置分页前的代码不会超出 2 MB 内存范围就没事

```assembly
PML4_BASE			equ	0x3000
PDPTE0_BASE			equ 0x4000
PDE0_BASE			equ 0x5000
```

然后配置页表

```assembly
	; 配置 PML4
	mov	eax, PDPTE0_BASE
	or	eax, 7
	mov	[PML4_BASE], eax

	; 配置 PDPTE
	mov eax, PDPTE0_BASE
	or	eax, 7
	mov	[PDPTE0_BASE], eax

	; 配置 2MB 大页
	mov	eax, 0x87
	mov	[PDE0_BASE], eax
```

都配置为 存在、可读写、系统页，其中 PDE 设为大页，再将 PML4 写入 `CR3`。由于页表本来就在头 2 MB 可以被访问到，所以暂时先不设置自引用

## 启用 IA-32e

设置 `IA32_EFER` MSR 寄存器的 `LME` 位

```assembly
IA32_EFER			equ 0xc0000080
```

```assembly
	; 启用 IA-32e
	mov ecx, IA32_EFER
	rdmsr

	bts eax, 8 ; 设置IA32_EFER.LME
	wrmsr
```

## 启用分页

```assembly
	; 启用分页
	mov	eax, cr0
	bts eax, 31
	mov cr0, eax
```

开启分页后，处理器就会自动设置 `IA32_EFER.LMA` 激活 IA-32e 模式，但是此时还不完全能执行 64 位指令，需要进入 64 位代码段

## 配置分段

首先定义一个 IA-32e 兼容模式下可用的 GDT:

```assembly
GDT:
	;GDT的第0个表项必须为0
	dd	0,0
	;1:64位 4GB代码段
	dw	0xffff, 0x0000
	db	0x00
	dw	0x2f9a
	db	0x00
	;2:64位 4GB数据段
	dw	0xffff, 0x0000
	db	0x00
	dw	0x0f92
	db	0x00
GDTR:
	dw	3*8-1
	dd	GDT
```

和之前 32 位版的GDT差别不大，主要是设置了 L 清除了 D/B 标志位

刷新段寄存器的操作也是一样的

```assembly
    lgdt [GDTR]
    jmp 0x08:flush

[bits 64]
flush:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

    mov ecx, LOADER_STACK_TOP
    mov esp, ecx
	cld
```

需要注意的是，跳转到 `kernel_early_start` 的时候需要先赋值给 `rax` 再 `jmp rax` 确保直接跳转到 64 位地址

```assembly
	mov rax, kernel_early_start
    jmp rax
```

## 解析 multiboot2 信息

然后就可以正常调用 `multiboot2_loader` 来解析了。记得把编译器参数改成输出 64 位 ELF 文件，同时要设置 `-mcmodel=large` ( gcc ) 和 `-C code_model=large` （ rustc )，因为 `.multiboot2` 和 `.early_init` 在低地址，需要获取到 64 位虚拟地址的符号时需要使用 large 模型来告诉编译器可以使用 64 位地址

```assembly
	; 32位下的cdecl ABI
    ; push esi
    ; push edi

	; 64位下的System V ABI
	; 前面已经设置过了
    call multiboot2_loader
	; add esp, 8
```

另外，由于 multiboot2 提供的 boot info 地址可能不在前 2 MB 内存里，需要在解析信息时临时映射一下

```c

void __multiboot2 temp_map(uint32_t base_addr) {
	int		pdpt_i = base_addr >> 30;
	size_t *pdpt   = (size_t *)PDPT0_BASE;

	int		pdt_i = (base_addr >> 21) & 0x1ff;
	size_t *pdt	  = (size_t *)PDT0_BASE;
	if (pdpt_i > 0) {
		pdt = (size_t *)PDPT_KBASE; // 临时借用
	}

	pdt[pdt_i] =
		(((size_t)base_addr & ~0x1fffff) | SIGN_HUGE | SIGN_SYS | SIGN_RW |
		 SIGN_P);

	pdpt[pdpt_i] = ((size_t)pdt & ~0xfff) | SIGN_SYS | SIGN_RW | SIGN_P;
}

void __multiboot2 temp_unmap(uint32_t base_addr) {
	int		pdpt_i = base_addr >> 30;
	size_t *pdpt   = (size_t *)PDPT0_BASE;

	int pdt_i = (base_addr >> 21) & 0x1ff;
	if (pdpt_i > 0) {
		pdpt[pdpt_i] = 0;
	} else if (pdt_i > 0) {
		size_t *pdt = (size_t *)PDT0_BASE;
		pdt[pdt_i]	= 0;
	}
}
```

这里借用了 `PDPT_KBASE` 的地址，只是使用这个页来当临时的 PDT，和 PDPT 没有关系

## 链接脚本

链接脚本也要相应的修改，这里由于 macOS 下的 `x86_64-elf-ld` 不支持 `elf_x86_64` （多少有点逆天），使用的 LLVM `ld.lld` 不支持 `OUTPUT_FORMAT` 删掉了这一行

这里将虚拟地址起点设为了 `0xffff_ffff_8000_0000` ，参考了 Linux 在 x86_64 下的设置

```lds 
OUTPUT_ARCH(x86_64)
ENTRY(_start)

/* 略 */

VIR_BASE = 0xffffffff80000000;
KERNEL_PHY_BASE = 0x00100000;

SECTIONS {
	/* 略 */
	/* data segment */
    .data : AT(ADDR(.data) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.data*)
    } : data

    .got : AT(ADDR(.got) - VIR_BASE) {
        . = ALIGN(8);
        *(.got)
        *(.got.plt)
    } : data

    /* bss segement */
    .bss : AT(ADDR(.bss) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.bss*)
    } : data

    _kernel_end_vir = .;
    _kernel_end_phy = _kernel_end_vir - VIR_BASE;

    /DISCARD/ : {
        *(.eh_frame*)
    }
}
```

## 重新配置页表

这里本着不信任引导程序建立的页表的原则，重新配置了页表

```c
	size_t *pml4 = (size_t *)PML4_BASE;
	size_t *pdpt = (size_t *)PDPT_KBASE;
	size_t *pdt	 = (size_t *)PDT0_BASE;

	// 重新配置页表
	pdt[0] = (0 | SIGN_HUGE | SIGN_SYS | SIGN_RW | SIGN_P);
	early_memset(&pdt[1], 0, PAGE_SIZE - sizeof(size_t));

	pdpt[0] = (PDT0_BASE | SIGN_SYS | SIGN_RW | SIGN_P);
	early_memset(&pdpt[1], 0, PAGE_SIZE - sizeof(size_t));

	pml4[0] = (PDPT_KBASE | SIGN_SYS | SIGN_RW | SIGN_P);
	early_memset(&pml4[1], 0, PAGE_SIZE - sizeof(size_t));
```

然后是从 `0xffff_ffff_8000_0000` 开始的 512 MB 直接映射到 0 开始的物理地址

```c
	early_memset((void *)PDPT_KBASE, 0, PAGE_SIZE);
	pml4[511] = (PDPT_KBASE | SIGN_SYS | SIGN_RW | SIGN_P);

	early_memset((void *)PDT_KBASE_START, 0, PAGE_SIZE);

	// 以 2MB 为单位映射内核和预分配内存，直到 512MB
	int page;

	pdt = (size_t *)PDT_KBASE_START;

	size_t addr = 0;
	size_t _2mb = 512 * PAGE_SIZE;

	for (page = 0; page < 256; page += 512) {
		*pdt = (addr | SIGN_HUGE | SIGN_RW | SIGN_SYS | SIGN_P);
		addr += _2mb;
		pdt = (size_t *)((size_t)pdt + PAGE_SIZE);
	}

	pdpt[510] = (PDT_KBASE_START | SIGN_SYS | SIGN_RW | SIGN_P);
```

此时内核程序就都可以正常运行了（只要不超过 512 MB 大小）

## 配置描述符

进入内核后还要重新配置一下 GDT, IDT 还有 TSS

### GDT

GDT 只是简单改了下 flag，基本上变化不大

```c
	// 内核代码段
	set_segment_descriptor(
		gdt + 1, 0xffffffff, 0x00000000,
		DESC_L | DESC_P | DESC_S | DESC_TYPE_CODE | DESC_DPL(0));
	// 内核数据段
	set_segment_descriptor(
		gdt + 2, 0xffffffff, 0x00000000,
		DESC_P | DESC_S | DESC_TYPE_DATA | DESC_DPL(0));
	// 用户代码段
	set_segment_descriptor(
		gdt + 3, 0xffffffff, 0x00000000,
		DESC_L | DESC_P | DESC_S | DESC_TYPE_CODE | DESC_DPL(3));
	// 用户数据段
	set_segment_descriptor(
		gdt + 4, 0xffffffff, 0x00000000,
		DESC_P | DESC_S | DESC_TYPE_DATA | DESC_DPL(3));
	// TSS
	set_segment_descriptor(
		gdt + 5, (uint32_t)(sizeof(struct tss_s) - 1), (uint64_t)(size_t)&tss,
		DESC_P | DESC_TYPE_TSS | DESC_DPL(0));
```

### IDT

IDT 由于结构变化了，需要修改一下配置函数

```c
struct gate_descriptor {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t	 ist;
	uint8_t	 access_right;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t reserved;
};
```

```c
void set_gate_descriptor(
	struct gate_descriptor *gd, uint64_t offset, uint16_t selector,
	uint8_t ar) {
	gd->offset_low	 = offset & 0xffff;
	gd->selector	 = selector;
	gd->ist			 = 0;
	gd->access_right = ar;
	gd->offset_mid	 = (offset >> 16) & 0xffff;
	gd->offset_high	 = (offset >> 32) & 0xffffffff;
	gd->reserved	 = 0;
	return;
}
```

## 迁移复盘

这次迁移过程中也碰到了不少的问题，让 AI 帮我总结了一下：

上面这些内容主要是从体系结构角度说明“怎样进入 64 位模式”，但真正把一个原本的 32 位内核迁过去时，问题往往并不出在某一条 `mov %cr0` 或 `ljmp` 指令上，而是出在大量仍然保留着 32 位假设的运行时实现上。

这次迁移里，真正踩到的坑基本都集中在下面几类。

### 不能只把寄存器改成 64 位

从 32 位迁到 64 位，不能只把 `eax` 改成 `rax`，或者把指针类型改成 `uint64_t`。下面这些东西必须一起迁：

- C 函数调用 ABI
- 异常入口和返回时的栈布局
- `switch_to` 保存和恢复的寄存器集合
- GDT/IDT/TSS 的描述符格式
- 页表层级和页表遍历接口
- C 和 Rust 之间暴露的页表与物理地址接口

只迁其中一部分，系统表面上可能已经进了长模式，但只要一触发异常、线程切换或者页表切换，就会立刻暴露出旧假设。

### 异常路径要最先收敛

这次最典型的问题之一，就是异常入口汇编和 C 处理函数仍然沿用 32 位时代的协议。

在 64 位下，CPU 压入的异常栈帧已经和 32 位不同，入口汇编传参方式也不同。如果汇编入口、`exception_handler()` 的参数定义、`iretq` 预期的返回栈布局三者不完全一致，就会出现下面这种现象：

- 明明已经触发了异常，但看起来“进不了异常入口”
- 首发异常后又连着触发 `#GP`
- 最后升级成 `#DF`

实际原因并不是 IDT 一定坏了，而是异常分发链上的 ABI 已经不自洽。

因此迁移时应该优先保证：

- 每个异常入口都明确知道自己传给 C 的是什么
- 有错误码和无错误码两类异常统一成同一种软件协议
- C 侧按照 64 位异常栈帧解析 `RIP/CS/RFLAGS/RSP`
- `iretq` 前的栈布局和入口压栈顺序严格对称

如果这一层没先稳定下来，后面的很多问题都会被伪装成“异常入口失效”。

### 64 位描述符不是把旧结构体放大就行

GDT 和 IDT 看似只是“描述符表”，但 64 位下它们和 32 位已经不完全一样了。

实际踩到的点包括：

- 64 位代码段必须设置 `L=1`，同时清除 `D/B`
- TSS 在 64 位下是 16 字节描述符，不再是普通 8 字节段描述符
- TSS 基址可能落在高地址，不能再截断成 32 位
- IDT 门描述符也变成了 16 字节，偏移量需要拆成 `low/mid/high`

这些问题的可怕之处在于，平时系统可能还能“看起来继续跑”，但一到异常分发、任务寄存器装载、重新读取代码段描述符时就会炸。

### 先区分首发异常和异常处理过程中的异常

这次排查过程中，很多看起来像“异常入口坏了”的问题，最后根因其实都在异常入口之前。

例如：

- 编译器生成了 `xmm` 指令，但内核没有明确决定是否支持 SSE
- `printk()` 开栈帧时使用了一个已经靠近栈底的坏栈
- 线程切换时把错误的物理地址写进 `CR3`

这些场景的共性是：

- 第一拍异常是真实业务错误
- 第二拍或第三拍才是“进不了异常处理程序”

因此遇到 `#PF -> #GP -> #DF` 或 `#UD -> #GP -> #DF` 时，首先要抓的是第一拍的 `RIP/CR2/CR3`，而不是先怀疑 IDT。

### 切线程和“首次启动线程”不是同一种调用

在 32 位内核里，常见做法是在线程栈上伪造一个返回地址，切进去后直接落到某个线程入口函数。这种思路在 64 位下不能直接照搬。

原因是 `switch_to` 做的是：

- 恢复一组保存寄存器
- 最后执行 `ret`

而一个正常的 64 位 C 函数调用遵循的是 System V ABI，参数走寄存器，不是靠栈上随便摆两个值就能自动生效。

因此：

- `switch_to` 保存/恢复的 `thread_stack` 必须和汇编顺序完全一致
- 新线程第一次被切入时，往往需要一个桥接入口，把恢复出来的寄存器转换成真正的 C 调用参数

如果忽略这一点，就会出现：

- `switch_to` 之后立即跑飞
- 第一次进入线程函数时参数完全不对
- 调用栈和调试器显示都很混乱

### SSE/XMM 指令必须是显式决策

迁到 64 位后，工具链比 32 位时代更容易自动生成 `xmm` 指令。这里不能糊里糊涂地依赖“现在似乎没事”。

必须明确二选一：

- 要么从编译选项上禁止生成这类指令
- 要么正确启用并管理 FPU/SSE 状态

如果这一点没有先定下来，内核里第一次出现 `pxor`、`movaps` 一类指令时，很可能直接触发 `#UD`。

### E820 的“可分配”和“可访问”是两回事

迁到 64 位后，物理地址空间里除了普通 RAM 之外，还会频繁接触到：

- framebuffer
- PCI BAR
- APIC / IOAPIC
- ACPI 表
- 其他固件保留区

这里最容易犯的错，是把 “E820 里出现了某段物理地址” 和 “这段物理地址应该进入 buddy 分配器” 混为一谈。

更合理的原则是：

- `type == 1` 的 `usable` 决定“能不能分配”
- MMIO、ACPI、framebuffer、保留区决定“要不要按需映射”

也就是说：

- 能访问，不代表能交给物理页分配器
- 不可分配，也不代表完全不需要映射

这对 framebuffer 和 PCI MMIO 尤其重要。

### MMIO 需要独立于 buddy 的所有权模型

这次还暴露出一个长期容易被忽略的问题：固定物理地址映射和普通匿名物理页，并不是同一种对象。

例如 `ioremap` 一个 PCI BAR 或 framebuffer 时：

- 这段物理页应该允许建立页表映射
- 但它并不属于 buddy 分配器
- `iounmap` 时也不应该把它“释放回 buddy”

如果继续复用普通匿名页的语义，很容易在释放时把 MMIO 当作可回收 RAM 处理，结果破坏物理页分配器状态。

因此更稳妥的做法是给这类固定映射单独建一类标签，例如：

- `AssignedFixed`

它表达的是：

- 可映射
- 可参与引用管理
- 但不归 buddy 所有
- `iounmap` 只丢弃映射，不回收物理页

### vmemmap 不能假设只在早期一次性建完

如果物理页元数据采用 `vmemmap` 方案，那么高地址 MMIO、保留区、framebuffer 等物理页也可能在运行时第一次被引用到。

这时不能假设所有 `Frame` 元数据页都已经在早期静态建好，否则固定映射路径一旦落到还没有 metadata backing 的物理范围，就会出现新的页故障或者直接误判。

因此更可靠的策略是：

- 在 `assign` 一类固定映射入口里，先检查目标物理范围对应的 `vmemmap` 页是否已经存在
- 缺失的元数据页按需补映射
- 然后再做 buddy 冲突判断和固定映射对象构造
