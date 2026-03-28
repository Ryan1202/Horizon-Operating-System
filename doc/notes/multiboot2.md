# Multiboot2 规范

要能够通过`multiboot2`来引导内核，首先的要求就是内核需要是`ELF`格式的可执行文件。

## 工具链

在多数情况下编译工具链输出的都是`ELF`格式，问题不大，不过Windows和macOS分别有自己的格式 `PE` 和 `Mach-O` 。我的解决方案是：在Windows下使用WSL提供的Linux环境，绕过Windows这个大坑（其实用cygwin之类的环境也可以）；而在macOS下，需要额外安装 `x86_64-elf-` 系列工具链（这是64位的工具链，不过编译32位二进制也问题不大。

## 格式

`multiboot2` 定义了一个header格式用来表示这是一个符合其协议的可执行文件，这个header必须位于文件的起始 `32KB` 范围内，格式如下：

| 偏移  | 类型 | 字段名        | Note     |
| ----- | ---- | ------------- | -------- |
| 0     | u32  | magic         | required |
| 4     | u32  | architecture  | required |
| 8     | u32  | header_length | required |
| 12    | u32  | checksum      | required |
| 16-XX |      | tags          |          |

- `magic` 是一个魔数，用来标识这是一个`multiboot2`的header

- `architecture` 用来标识CPU的架构，`0`指x86 32位保护模式，`4`指32位 MIPS
- `header_length` 是这个头部的长度，由于 `tags` 数量不确定所以并不是固定的
- `checksum` 是校验和，用来验证这个头部是否有效

## Tag格式

所有的 `Tag` 有一个同样的头部，即

```
    +-------------------+
u16 | type              |
u16 | flags             |
u32 | size              |
    +-------------------+
```

如果 `flags` 的最低位为1，说明这个 `Tag` 引导程序是可以被忽略的

下面介绍几个需要用到的 `Tag` :

### Multiboot2 information request

```
        +-------------------+
u16     | type = 1          |
u16     | flags             |
u32     | size              |
u32[n]  | mbi_tag_types     |
        +-------------------+
```

这里的 `mbi_tag_types` 是另外的 `tag` ，用来填入需要请求的信息（如内存信息、VRAM地址、ACPI地址等等）

这里我需要用到的 `mbi_tag` 有：

```assembly
MULTIBOOT2_TAG_MMAP			equ 6 ; 内存分布信息
MULTIBOOT2_TAG_VBE			equ 7 ; VBE信息（包括显示模式信息和控制信息）
MULTIBOOT2_TAG_FRAMEBUFFER	equ	8 ; VRAM地址
```

### framebuffer 

```
    +-------------------+
u16 | type = 5          |
u16 | flags             |
u32 | size = 20         |
u32 | width             |
u32 | height            |
u32 | depth             |
    +-------------------+
```

指定显示模式信息，包括三个参数：宽度(`width`)、高度(`height`) 和 颜色深度(`depth`)

注意深度是以比特为单位，我这里要指定的是 `1024x768x32bit` 的显示模式，则深度应该是`32`

### padding

就是纯 `0`，没有特别的格式，随便几个字节，一般用来对齐

### end

表示 `tag` 结束，`type` 为0，`size`为8

## 启动时的状态

`multiboot2` 规范还指定了启动（内核）时的机器状态，对于32位x86机器(i386)来说如下：

| 寄存器         | 状态                                              |
| -------------- | ------------------------------------------------- |
| EAX            | 魔数 `0x36d76289`                                 |
| EBX            | 一个32位物理地址，指向Multiboot2提供的信息        |
| CS             | 指向32位可读可执行段，偏移为0，大小为`0xFFFFFFFF` |
| DS,ES,FS,GS,SS | 32位可读写段，偏移和大小同上                      |
| A20门          | 开启                                              |
| CR0            | CR0.PG（分页）清除，CR0.PE（保护模式）设置        |
| EFLAGS         | VM（虚拟8086模式）和IF（中断）清除                |
| ESP            | 需要OS自己创建一个栈来设置                        |
| GDTR           | GDTR可能是无效的，OS必须自己配置GDT               |
| IDTR           | OS必须在开启中断前配置好IDT                       |



# 链接脚本

链接器使用`ld`，所以相应的使用lds脚本来配置

```lds
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(_start) /* 入口为汇编中的_start标签 */

PHDRS
{
    text_phy PT_LOAD FLAGS(5); /* R_E: 可读 + 可执行 */
    text PT_LOAD FLAGS(5); /* R_E: 可读 + 可执行 */
    data PT_LOAD FLAGS(6); /* R_W: 可读 + 可写 */
}

KERNEL_PHY_BASE = 0x00100000; /* 内核的起始物理地址 */

SECTIONS {
    . = KERNEL_PHY_BASE;

    /* multiboot2位置要尽量靠前 */
    .multiboot2 ALIGN(8) : {
        KEEP(*(.multiboot2))
    } : text_phy
    
    /* code segment */
    .text : {
        . = ALIGN(4);   /* 4 bytes align */
        *(.text*)
    } : text
    
    /* data segment */
    .data : {
        . = ALIGN(4);   /* 4 bytes align */
        *(.data)
    } : data

    /* bss segement */
    .bss : {
        . = ALIGN(4);   /* 4 bytes align */
        *(.bss*)
    } : data
}
```



这里只是最简单的配置，主要在于把`.multiboot2`段放在尽可能最前面，让引导程序能够找到`multiboot2_header`

# 汇编部分

虽然主体部分使用c编写，但是刚启动时需要现在汇编下配置好环境

首先先定义到 `.multiboot2` 段，配置一下保护模式需要的临时 `GDT`，`multiboot2_header` 插入在代码中间，使用跳转指令跳过

以下是 nasm 汇编：

```assembly
[bits 32]

[section .multiboot2]

extern kernel_early_start, multiboot2_loader ; 导入的c函数
global _start

LOADER_STACK_TOP	equ	0x90000	; 临时的栈地址

_start:
	jmp start
	; 临时的GDT
GDT:
	;GDT的第0个表项必须为0
	dd	0,0
	;1:4GB代码段
	dd	0x0000ffff
	dd	0x00cf9a00
	;2:4GB数据段
	dd	0x0000ffff
	dd	0x00cf9200
GDTR:
	dw	3*8-1
	dd	GDT

;定义为宏防止出现在符号表中而被错误读取
%define MULTIBOOT2_HEADER_MAGIC 	0xe85250d6
%define MULTIBOOT2_HEADER_ARCH		0x00000000
%define MULTIBOOT2_HEADER_LENGTH	(multiboot2_header_end - multiboot2_header)
%define MULTIBOOT2_HEADER_CHKSUM	-(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_HEADER_ARCH + MULTIBOOT2_HEADER_LENGTH)

MULTIBOOT2_HEADER_TAG_END			equ	0
MULTIBOOT2_HEADER_TAG_INFO_REQ		equ	1
MULTIBOOT2_HEADER_TAG_FRAMEBUFFER	equ	5

MULTIBOOT2_TAG_MMAP			equ	6
MULTIBOOT2_TAG_VBE			equ	7
MULTIBOOT2_TAG_FRAMEBUFFER	equ	8

VIDEOMODE_WIDTH     equ 1024
VIDEOMODE_HEIGHT    equ 768
VIDEOMODE_DEPTH     equ 32

	align 8
multiboot2_header:
	dd	MULTIBOOT2_HEADER_MAGIC
	dd	MULTIBOOT2_HEADER_ARCH
	dd	MULTIBOOT2_HEADER_LENGTH
	dd	MULTIBOOT2_HEADER_CHKSUM
info_tag_start:
	dw	MULTIBOOT2_HEADER_TAG_INFO_REQ
	dw	0
	dd	info_tag_end - info_tag_start
	dd	MULTIBOOT2_TAG_MMAP
	dd	MULTIBOOT2_TAG_FRAMEBUFFER
	dd	MULTIBOOT2_TAG_VBE
info_tag_end:
	dd	0	;对齐8字节
framebuffer_tag_start:
    dw  MULTIBOOT2_HEADER_TAG_FRAMEBUFFER
    dw  0
    dd  framebuffer_tag_end - framebuffer_tag_start
    dd  VIDEOMODE_WIDTH
    dd  VIDEOMODE_HEIGHT
    dd  VIDEOMODE_DEPTH
framebuffer_tag_end:
	dd	0	;对齐8字节
end_tag_start:
	dw	MULTIBOOT2_HEADER_TAG_END
	dw	0
	dd	end_tag_end - end_tag_start
end_tag_end:
multiboot2_header_end:
	; 正式开始执行，首先先配置GDT刷新段寄存器
start:
    cli
    lgdt [GDTR]
    jmp 0x08:flush
flush:
	mov edx, eax	; 后面需要检查eax先临时保存
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

    mov ecx, LOADER_STACK_TOP	; 临时的栈
    mov esp, ecx
	cld

	mov eax, edx
	
    push ebx
    push eax
    call multiboot2_loader		; 进入loader的c部分
	add esp, 8

    jmp kernel_early_start		; 跳转到内核入口

```

# C 部分

这里使用了 `__multiboot2` 宏来将函数放到 `.multiboot2` 段中去

```c
#define __multiboot2 __attribute__((section(".multiboot2")))
```

接下来的c代码主要负责将引导程序提供的信息转换并复制到内核中的全局变量里去，其中 `ARDS_ADDR` 是我手动分配的内存布局存储地址，设置为低地址中的 `0x2000`。顺带一提，我为正式的 GDT 和 IDT 划的内存在 `0x1000` ~ `0x1fff`

部分结构定义就略去了，具体可以参见VBE标准和multiboot2标准

```c
extern struct VesaDisplayInfo vesa_display_info;

void __multiboot2 multiboot2_memcpy(void *dst, void *src, size_t size) {
	uint8_t *_dst = dst, *_src = src;
	for (size_t i = 0; i < size; i++) {
		*_dst++ = *_src++;
	}
}

void __multiboot2 multiboot2_loader(uint32_t eax, uint32_t ebx) {
	if (eax != 0x36d76289) {
		while (true)
			;
	}
	// 读取multiboot2信息
	uint32_t *p			 = (uint32_t *)ebx;
	uint32_t  total_size = *p;

	struct VesaDisplayInfo *vesa_info =
		(struct VesaDisplayInfo *)vaddr2paddr(&vesa_display_info);

	int i = 0;
	p += 2;
	while (i < total_size) {
		uint32_t type = *p;
		uint32_t size = *(p + 1);
		switch (type) {
		case MBIT_FRAMEBUFFER_INFO: {
			struct framebuffer_tag *fb = (struct framebuffer_tag *)p;
			vesa_info->vram_phy		   = (uint8_t *)fb->framebuffer_addr[0];
			vesa_info->width		   = fb->framebuffer_width;
			vesa_info->height		   = fb->framebuffer_height;
			vesa_info->BitsPerPixel	   = fb->framebuffer_bpp;
			break;
		}
		case MBIT_VBE_INFO: {
			struct vbe_info_tag *vbe = (struct vbe_info_tag *)p;
			multiboot2_memcpy(
				&vesa_info->vbe_mode_info, vbe->vbe_mode_info,
				sizeof(struct VbeModeInfoBlock));
			multiboot2_memcpy(
				&vesa_info->vbe_control_info, vbe->vbe_control_info,
				sizeof(struct VbeControlInfoBlock));
			break;
		}
		case MBIT_MEM_MAP: {
			struct mem_map_tag *mmap = (struct mem_map_tag *)p;

			int entry_count = (mmap->size - 16) / mmap->entry_size;
			for (int j = 0; j < entry_count; j++) {
				multiboot2_memcpy(
					(void *)ARDS_ADDR + j * sizeof(struct ards),
					(void *)p + 16 + j * mmap->entry_size, sizeof(struct ards));
			}
			*((uint16_t *)ARDS_NR) = entry_count;
			break;
		}
		case MBIT_END: {
			i = total_size;
			break;
		}
		default:
			break;
		}
		// Tag要对齐8字节
		if (size & 0x07) { size = (size + 8) & (~0x07); }
		i += size;
		size >>= 2;
		p += size;
	}
}
```

---

接下来就可以正式进入内核了

# 参考资料

[The Multiboot2 Specification version 2.0](https://www.gnu.org/software/grub/manual/multiboot2/)