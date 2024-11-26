KERNEL_STACK_TOP_PHY EQU 0x0009fc00
KERNEL_STACK_TOP_VIR EQU 0x8009fc00

[bits 32]

[section .text]

extern main, setup_page
global kernel_start
	
kernel_start:	
	mov ax, 0x10	;the data 
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	mov gs, ax 
	mov ss, ax 
	mov esp, KERNEL_STACK_TOP_PHY - 4
	
	;在正式进入内核前配置好分页
	call setup_page
	mov	eax,	cr0
	or	eax,	0x80000000
	mov cr0,	eax
	call main
	
Sleep:
	hlt
	jmp Sleep
	
	jmp $	