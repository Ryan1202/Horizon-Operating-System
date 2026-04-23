在内核启动的早期，我们需要先配置好页表还有GDT、IDT等等与内存相关的配置，确保进入内核主程序后的代码能够正常运行

# 刚刚进入内核

首先先定义内核栈的物理地址和虚拟地址：

```assembly
KERNEL_STACK_TOP_PHY EQU 0x0009fc00
KERNEL_STACK_TOP_VIR EQU 0xc009fc00
```

然后进入启动早期：

```assembly
[bits 32]

[section .early_init]

extern page_early_setup
global kernel_early_start

kernel_early_start:
	mov ax, 0x10	;the data 
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	mov gs, ax 
	mov ss, ax 
	mov esp, KERNEL_STACK_TOP_PHY - 4

	call page_early_setup
	mov cr3, eax

	; 启用分页和写保护
	mov	eax,	cr0
	or	eax,	0x80008000
	mov cr0,	eax

	; 启用全局页和页大小扩展
	mov eax,	cr4
	or	eax,	0x00000050
	mov cr4,	eax

	jmp kernel_start
```

这里导入和调用了c函数 `page_early_setup` 用来先构建一个勉强能跑起来的页表以便直接切换到虚拟地址，通过写入 `cr0` 来启用分页和页的写保护，再写入了 `cr4` 启用全局页和页大小扩展（在32位模式下是单个页可以扩展为 `4MB` 的大页）以备未来需要

## 简单初始化页表

接下来的两个c函数都使用了 `__early_init` 宏来标记这两个函数需要放到 `early_init` 段（因为还没开启分页，使用的还是物理地址）

```c
extern void	 *_kernel_start_phy, *_kernel_start_vir;
extern void	 *_kernel_end_phy, *_kernel_end_vir;
extern size_t PREALLOCATED_END_PHY;
extern size_t VIR_BASE[];

void __early_init early_memset(void *dst, uint8_t value, size_t size) {
	uint8_t *_dst = dst;
	for (int i = 0; i < size; i++)
		*_dst++ = value;
}

size_t __early_init page_early_setup(void) {
	size_t pdt_addr =
		((((size_t)&_kernel_end_phy) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1));
	size_t pt_addr_start = pdt_addr + PAGE_SIZE;

	size_t *pdt = (size_t *)pdt_addr;
	early_memset(pdt, 0, PAGE_SIZE);
	for (int i = 0; i < PAGE_SIZE / sizeof(size_t) - 1; i++) {
		pdt[i] = 0;
	}

	pdt[1023] =
		(pdt_addr | SIGN_RW | SIGN_SYS |
		 SIGN_P); // 第1023个页(最后4MB内存):页表

	// 第0个页(前4MB内存):GDT、BIOS
	size_t *pt = (size_t *)pt_addr_start;
	pdt[0]	   = ((size_t)pt | SIGN_RW | SIGN_SYS | SIGN_P);

	pt[0] = (0 | SIGN_RW | SIGN_SYS) &
			~SIGN_P; // 0x00000000-0x00000fff设为不存在，暴露NULL指针引发的问题
	size_t addr = PAGE_SIZE;
	for (int i = 1; i < PAGE_SIZE / sizeof(size_t); i++) {
		pt[i] = addr | SIGN_RW | SIGN_SYS | SIGN_P;
		addr += PAGE_SIZE;
	}

	int page;
	int kernel_start_page = (((size_t)&_kernel_start_vir) >> 12) & ~0x3ff;
	int kernel_end_page	  = (((size_t)&_kernel_end_vir) + PAGE_SIZE - 1) >> 12;
	addr				  = (size_t)&_kernel_start_phy & ~0x3fffff;

	for (page = kernel_start_page; page < kernel_end_page; page++) {
		if ((page & ((PAGE_SIZE) / sizeof(size_t) - 1)) == 0) {
			pt = (size_t *)((size_t)pt + PAGE_SIZE);
			early_memset(pt, 0, PAGE_SIZE);
			pdt[page >> 10] = (size_t)pt | SIGN_RW | SIGN_SYS | SIGN_P;
		}
		int num = page & ((PAGE_SIZE) / sizeof(size_t) - 1);
		pt[num] = (addr | SIGN_RW | SIGN_SYS | SIGN_P);
		addr += PAGE_SIZE;
	}

	size_t *preallocated_end =
		(size_t *)((size_t)&PREALLOCATED_END_PHY - (size_t)VIR_BASE);
	*preallocated_end = (size_t)pt + PAGE_SIZE;
	return (size_t)pdt;
}
```

这里初始化了两个部分的页表：

- 一个是 `0x00000000` 到 `0x00400000` 的 `4MB` 内存，保证这部分地址临时还能用

- 另一部分就是使用 `_kernel_start_vir` , `_kernel_end_vir` , `_kernel_start_phy` , `kernel_end_phy` 来计算内核覆盖的虚拟地址和物理地址范围，只将这一部分的页表先初始化保证内核的代码能正常运行
- 另外还使用了一个小技巧，不用额外分配页表所需的内存：将 `PDT` （页目录表，第二级页表）的最后一项设为自己的地址，这样的话访问最后 `4MB` 的内存就相当于在访问页表，每 `4KB` 都是一个页表，最后 `4KB` 就是页目录表自身

这里面还使用了一个极其简易的内存分配器：

- 使用内核程序结束的位置的下一个页作为分配的起始地址
- 每当需要分配一个页，就将分配器的指针 (复用了 `pt` 作为分配器指针) 增加一个页的大小

为了能够和内核后面的内存分配不冲突，将完成分配后的指针地址存入到全局变量 `PREALLOCATED_END_PHY` 中。需要注意的是，变量名中的 `PHY` 表示它存储的是物理地址，但是变量本身的地址使用的是虚拟地址，所以需要转换成物理地址才能使用

# 内核初始化

完成了能用的页表初始化之后，就进入了 `kernel_start` :

```assembly
[section .text]

extern main, kernel_early_init
global kernel_start

kernel_start:
	mov esp, KERNEL_STACK_TOP_VIR - 4

	;在正式进入内核前配置好分页
	call kernel_early_init

	call main
	
Sleep:
	hlt
	jmp Sleep
	
	jmp $	
```

## kernel_early_init

这里重新将 `esp` 设置为栈顶的虚拟地址，然后调用c函数 `kernel_early_init` 完成配置

```c
void kernel_early_init(void) {
	platform_early_init();
	memory_early_init();
}
```

### platform_early_init

先调用 `platform_early_init` 配置好段描述符和中断描述符

```c
// 完成一些平台必要的准备工作
void platform_early_init() {
	// 初始化段描述符和中断描述符
	init_descriptor();

	// 读取CPU特性
	read_features();
	if (cpu_check_feature(CPUID_FEAT_PAT)) {
		uint32_t lo, hi;
		cpu_rdmsr(IA32_PAT, &lo, &hi);
		hi = (hi & ~0x7) | 0x1; // 把 PAT4 设为 WriteCombining
		cpu_wrmsr(IA32_PAT, lo, hi);
	}
}
```

这里先调用了 `init_descriptor` 配置正式的 `GDT` , `IDT` 和 `TSS` ，然后通过 `CPUID` 指令识别CPU支持的特性并通过 `MSR` 寄存器开启 `PAT` 属性（用于支持更多的页缓存机制），详见英特尔软件开发者手册卷3A 13.12节 *Page Attribute Table*

```c
void init_descriptor(void) {
	size_t page_addr = (size_t)VIR_BASE + GDT_BASE;
	gdt				 = (struct segment_descriptor *)page_addr;
	idt				 = (struct gate_descriptor *)(page_addr + GDT_SIZE + 1);
	memset(&tss, 0, sizeof(struct tss_s));
	tss.ss0		= SELECTOR_K_STACK;
	tss.io_base = sizeof(struct tss_s);

	// 配置GDT
	int i;
	for (i = 0; i < GDT_SIZE / 8; i++) {
		set_segment_descriptor(gdt + i, 0, 0, 0);
	}
	// 内核代码段
	set_segment_descriptor(
		gdt + 1, 0xffffffff, 0x00000000,
		DESC_D | DESC_P | DESC_S_CODE | DESC_TYPE_CODE | DESC_DPL_0);
	// 内核数据段
	set_segment_descriptor(
		gdt + 2, 0xffffffff, 0x00000000,
		DESC_D | DESC_P | DESC_S_DATA | DESC_TYPE_DATA | DESC_DPL_0);
	// 用户代码段
	set_segment_descriptor(
		gdt + 3, 0xffffffff, 0x00000000,
		DESC_D | DESC_P | DESC_S_CODE | DESC_TYPE_CODE | DESC_DPL_3);
	// 用户数据段
	set_segment_descriptor(
		gdt + 4, 0xffffffff, 0x00000000,
		DESC_D | DESC_P | DESC_S_DATA | DESC_TYPE_DATA | DESC_DPL_3);
	// TSS
	set_segment_descriptor(
		gdt + 5, sizeof(struct tss_s), (int)&tss,
		DESC_D | DESC_P | DESC_S_SYS | DESC_TYPE_TSS | DESC_DPL_0);

	// 改变GDTR寄存器使其指向刚配置好的GDT
	load_gdtr(GDT_SIZE, (size_t)gdt);
	__asm__ __volatile__("ltr %w0" ::"r"(SElECTOR_TSS));

	// 配置IDT
	// 0x00-0x1f号中断是CPU异常中断, 0x20-0x2f号中断是IRQ中断
	for (i = 0; i < IDT_SIZE / 8; i++) {
		set_gate_descriptor(idt + i, 0, 0, 0);
	}
	set_gate_descriptor(
		idt + 0x00, (int)&exception_entry0, 0x08, DA_386IGate_DPL0);
    // 到 exception 0x1f，略

	set_gate_descriptor(
		idt + 0x20 + 0, (int)&irq_entry0, 0x08, DA_386IGate_DPL0);
    // 到 irq 15, 略
    
	for (i = 0; i < NR_IRQ; i++) {
		irq_table[i] = default_irq_handler;
	}

	set_gate_descriptor(
		idt + 0x80, (int)syscall_handler, 0x08, DA_386IGate_DPL3);

	load_idtr(IDT_SIZE, (size_t)idt);
}
```

就是普通的段描述符配置，不做赘述

### memory_early_init

早期初始化内存配置，读取了引导程序提供的内存布局信息，先调用`page_early)init` 初始化早期内存分配器，调用 `setup_page` 完整配置好页表

```c
void memory_early_init(void) {
	page_early_init((size_t)&_kernel_end_phy);
	setup_page();
}
```

`setup_page` 主要就是将线性映射区 ( `0xc0000000` - `0xf0000000` 的页表设置好 ) ，另外就是将页表所使用的页设置为不存在，只能通过最后 `4MB` 的内存地址修改页表

```c
void setup_page(void) {
	pdt_phy_addr =
		((((size_t)&_kernel_end_phy) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1));
	// 物理地址->虚拟地址映射关系:
	// 0x00000000-0x00400000 => 0x00000000-0x00400000
	// 0x00000000-0x37ffffff => 0xc0000000-0xf7ffffff

	// 启动时只映射内核部分内存和预分配内存
	size_t end = KERNEL_LINEAR_SIZE;

	size_t *pdt		= (size_t *)0xfffff000;
	int		pdt_off = ((size_t)VIR_BASE) >> 22;

	size_t kernel_end = page_align_up((size_t)&_kernel_end_phy);
	// 预分配内存占用的页数
	size_t page_delta = (end - kernel_end) >> 12;

	// 由于需要计算要额外分配的页表数，
	// 所以需要向上对齐到页目录表项，忽略已分配过的最后一个页表
	size_t end_pdt		  = ((end - 1) + 0x003fffff) >> 22;
	size_t kernel_end_pdt = (kernel_end + 0x003fffff) >> 22;

	// 管理预分配内存所需的页目录表项数，
	// 一个页目录表项对应一个页表，一个页表需要额外分配一个页
	size_t pdt_delta = end_pdt - kernel_end_pdt;

	int page_start = kernel_end >> 12;
	int page_end   = page_start + page_delta + pdt_delta;

	size_t start_addr = early_allocate_pages(pdt_delta);
	size_t addr		  = start_addr;

	int		pdt_num;
	int		pt_num;
	int		page = page_start;
	size_t *pt, *pde;

	for (pdt_num = kernel_end_pdt - 1; pdt_num < end_pdt; pdt_num++) {
		pde = (size_t *)&pdt[pdt_off + pdt_num];
		pt	= (size_t *)(0xffc00000 + ((pdt_off + pdt_num) << 12));
		if (!(*pde & 0xfffff000)) {
			*pde = ((size_t)addr | SIGN_RW | SIGN_SYS | SIGN_P);
			addr += PAGE_SIZE;
		}
		pt_num = (page & 0x3ff);
		for (int i = pt_num; i < 1024 && page < page_end; i++, page++) {
			pt[i] = (page << 12 | SIGN_RW | SIGN_SYS | SIGN_P);
		}
	}

	// 将页表所在的页设置为不存在，这样只能通过映射到最后4MB的页修改页表
	// 避免代码bug导致意外修改
	for (int i = 0; i < 1024; i++) {
		if (pdt[i] & SIGN_P && pdt[i] & 0xfffff000) {
			size_t phy_addr = pdt[i] & 0xfffff000;
			size_t vir_addr = (size_t)VIR_BASE + phy_addr;

			size_t *pte = pte_ptr(vir_addr);
			(*pte) &= ~SIGN_P;
		}
	}
}
```

## main

到这里为止，CPU硬件方面的的配置基本就完成的差不多了，可以正式进入主函数 `main` 了

```c
int main() {
    init_memory();
    /* 略 */
}
```

