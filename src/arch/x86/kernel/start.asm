KERNEL_STACK_TOP_PHY EQU 0x0009fc00
KERNEL_STACK_TOP_VIR EQU 0xffffffff_8009fc00

[bits 64]

[section .early_init]

extern page_early_setup
global kernel_early_start

kernel_early_start:
	mov rsp, KERNEL_STACK_TOP_PHY - 8

	call page_early_setup

	; 启用写保护
	mov	rax,	cr0
	bts	rax,	16
	mov cr0,	rax

	; 启用全局页
	mov rax,	cr4
	bts	rax,	7
	mov cr4,	rax

	mov	rax,	kernel_start
	jmp rax

[section .text]

extern main, kernel_early_init
global kernel_start

kernel_start:
	mov rsp, KERNEL_STACK_TOP_VIR - 8

	;在正式进入内核前配置好分页
	call kernel_early_init

	call main
	
Sleep:
	hlt
	jmp Sleep
	
	jmp $	