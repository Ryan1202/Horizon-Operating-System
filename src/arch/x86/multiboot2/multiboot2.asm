[bits 32]

[section .multiboot2]

extern kernel_early_start, multiboot2_loader
global _start

LOADER_STACK_TOP	equ	0x90000
PML4_BASE			equ	0x3000
PDPTE0_BASE			equ 0x4000
PDE0_BASE			equ 0x5000

IA32_EFER			equ 0xc0000080

_start:
	jmp start
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

;定义为宏防止出现在符号表中而被错误读取
%define MULTIBOOT2_HEADER_MAGIC 	0xe85250d6
%define MULTIBOOT2_HEADER_ARCH		0x00000000
%define MULTIBOOT2_HEADER_LENGTH	(multiboot2_header_end - multiboot2_header)
%define MULTIBOOT2_HEADER_CHKSUM	-(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_HEADER_ARCH + MULTIBOOT2_HEADER_LENGTH)

MULTIBOOT2_TAG_ALIGN		equ	8
MULTIBOOT2_TAG_END			equ	0
MULTIBOOT2_TAG_CMDLINE		equ	1
MULTIBOOT2_TAG_LDR_NAME		equ	2
MULTIBOOT2_TAG_MODULE		equ	3
MULTIBOOT2_TAG_MEMINFO		equ	4
MULTIBOOT2_TAG_BOOTDEV		equ	5
MULTIBOOT2_TAG_MMAP			equ	6
MULTIBOOT2_TAG_VBE			equ	7
MULTIBOOT2_TAG_FRAMEBUFFER	equ	8
MULTIBOOT2_TAG_ELF_SECTIONS	equ	9
MULTIBOOT2_TAG_APM			equ	10
MULTIBOOT2_TAG_EFI32		equ	11
MULTIBOOT2_TAG_EFI64		equ	12
MULTIBOOT2_TAG_SMBIOS		equ	13
MULTIBOOT2_TAG_ACPI_OLD		equ	14
MULTIBOOT2_TAG_ACPI_NEW		equ	15
MULTIBOOT2_TAG_NETWORK		equ	16
MULTIBOOT2_TAG_EFI_MMAP		equ	17
MULTIBOOT2_TAG_EFI_BS		equ	18
MULTIBOOT2_TAG_EFI32_IH		equ	19
MULTIBOOT2_TAG_EFI64_IH		equ	20
MULTIBOOT2_TAG_LOAD_ADDR	equ	21

MULTIBOOT2_HEADER_TAG_END			equ	0
MULTIBOOT2_HEADER_TAG_INFO_REQ		equ	1
MULTIBOOT2_HEADER_TAG_ADDRESS		equ	2
MULTIBOOT2_HEADER_TAG_ENTRY_ADDR	equ	3
MULTIBOOT2_HEADER_TAG_CONSOLE_FLAGS	equ	4
MULTIBOOT2_HEADER_TAG_FRAMEBUFFER	equ	5
MULTIBOOT2_HEADER_TAG_MODULE_ALIGN	equ	6
MULTIBOOT2_HEADER_TAG_EFI_BS		equ	7
MULTIBOOT2_HEADER_TAG_ENTRY_EFI32	equ	8
MULTIBOOT2_HEADER_TAG_ENTRY_EFI64	equ	9
MULTIBOOT2_HEADER_TAG_RELOCATABLE	equ	10

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
start:
    cli
	mov edi, eax
	mov esi, ebx

	mov eax, cr4
	bts eax, 5		; 启用 PAE
	mov cr4, eax

	; 配置 PML4
	mov	eax, PDPTE0_BASE
	or	eax, 7
	mov	[PML4_BASE], eax

	; 配置 PDPTE
	mov eax, PDE0_BASE
	or	eax, 7
	mov	[PDPTE0_BASE], eax

	; 配置 2MB 大页
	mov	eax, 0x87
	mov	[PDE0_BASE], eax

	mov eax, PML4_BASE
	or	eax, 7
	mov	cr3, eax

	; 启用 IA-32e
	mov ecx, IA32_EFER
	rdmsr

	bts eax, 8 ; 设置IA32_EFER.LME
	wrmsr

	; 启用分页
	mov	eax, cr0
	bts eax, 31
	mov cr0, eax

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
	
	; 32位下的cdecl ABI
    ; push esi
    ; push edi

	; 64位下的System V ABI
	; 前面已经设置过了
    call multiboot2_loader
	; add esp, 8

	mov rax, kernel_early_start
    jmp rax