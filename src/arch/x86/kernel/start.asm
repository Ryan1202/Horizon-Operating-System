KERNEL_STACK_TOP EQU 0x009fc00

[bits 32]
[section .text]

extern main

global _start

_start:
	jmp start
	align 8
	
;multiboot_header:
;MULTIBOOT_HEADER_MAGIC	equ	0xe85250d6
;MULTIBOOT_HEADER_ARCH	equ	0x00000000
;MULTIBOOT_HEADER_LENGTH	equ multiboot_header_end - multiboot_header
;MULTIBOOT_HEADER_CHKSUM	equ	-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_ARCH + MULTIBOOT_HEADER_LENGTH)
;[section .data]
;	dd	0xe85250d6
;	dd	0x00000000
;	dd	multiboot_header_end - multiboot_header
;	dd	-(0xe85250d6 + 0x00000000 + (multiboot_header_end - multiboot_header))
	
;[section .text]
;multiboot_header_end:
start:
	;put 'K'
	mov byte [0x000b8000+160*1+0], 'K'
	mov byte [0x000b8000+160*1+1], 0x07
	mov byte [0x000b8000+160*1+2], 'E'
	mov byte [0x000b8000+160*1+3], 0x07
	mov byte [0x000b8000+160*1+4], 'R'
	mov byte [0x000b8000+160*1+5], 0x07
	mov byte [0x000b8000+160*1+6], 'N'
	mov byte [0x000b8000+160*1+7], 0x07
	mov byte [0x000b8000+160*1+8], 'E'
	mov byte [0x000b8000+160*1+9], 0x07
	mov byte [0x000b8000+160*1+10], 'L'
	mov byte [0x000b8000+160*1+11], 0x07
	
	mov ax, 0x10	;the data 
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	mov gs, ax 
	mov ss, ax 
	mov esp, KERNEL_STACK_TOP - 4
	
	call main
	
Sleep:
	hlt
	jmp Sleep
	
	jmp $	