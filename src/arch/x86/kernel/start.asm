KERNEL_STACK_TOP_PHY EQU 0x0009fc00
KERNEL_STACK_TOP_VIR EQU 0xc009fc00

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