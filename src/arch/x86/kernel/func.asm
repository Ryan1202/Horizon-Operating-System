	global io_in8,		io_out8,	io_in16,	io_out16,	io_in32,	io_out32
	global io_read,		io_write
	global io_cli,		io_sti,		io_hlt,		io_stihlt
	global read_cr3,	write_cr3,	read_cr2,	read_cr0,	write_cr0,	enable_paging
	global load_gdtr,	load_idtr
	global io_load_eflags,			io_store_eflags
	global divide_error
	global single_step_exception
	global nmi
	global breakpoint_exception
	global stack_exception
	global general_protection
	global page_fault
	global IRQ_timer,	IRQ_pit,	IRQ_keyboard,
	global thread_intr_exit
	global switch_to

extern exception_handler
extern irq_table
extern do_irq
extern do_syscall
extern apic_eoi

EOI				equ	0x20
INT_M_CTL		equ	0x20
INT_M_CTLMASK	equ	0x21
INT_S_CTL		equ	0xa0
INT_S_CTLMASK	equ	0xa1

%define   ERROR_CODE   nop
%define   NO_ERROR_CODE push 0

%macro INTERRUPT_ENTRY 1
global irq_entry%1
irq_entry%1:
	
	push 0

	push ds
	push es
	push fs
	push gs
	pushad
	
	mov dx, ss
	mov ds, dx
	mov es, dx
	mov fs, dx
	mov gs, dx
	
	push %1+0x20
	push %1
	call do_irq
	add esp, 4
	
	jmp intr_exit

%endmacro

%macro EXCEPTION_ENTRY 2
global exception_entry%1
exception_entry%1:
    %2				 ; 中断若有错误码会压在eip后面 
    push %1
    
    push esp
    call exception_handler
    add esp, 4*3
    
    hlt
%endmacro

[section .text]
[bits 32]

EXCEPTION_ENTRY 0,NO_ERROR_CODE
EXCEPTION_ENTRY 1,NO_ERROR_CODE
EXCEPTION_ENTRY 2,NO_ERROR_CODE
EXCEPTION_ENTRY 3,NO_ERROR_CODE 
EXCEPTION_ENTRY 4,NO_ERROR_CODE
EXCEPTION_ENTRY 5,NO_ERROR_CODE
EXCEPTION_ENTRY 6,NO_ERROR_CODE
EXCEPTION_ENTRY 7,NO_ERROR_CODE 
EXCEPTION_ENTRY 8,ERROR_CODE
EXCEPTION_ENTRY 9,NO_ERROR_CODE
EXCEPTION_ENTRY 10,ERROR_CODE
EXCEPTION_ENTRY 11,ERROR_CODE 
EXCEPTION_ENTRY 12,NO_ERROR_CODE
EXCEPTION_ENTRY 13,ERROR_CODE
EXCEPTION_ENTRY 14,ERROR_CODE
EXCEPTION_ENTRY 15,NO_ERROR_CODE 
EXCEPTION_ENTRY 16,ERROR_CODE
EXCEPTION_ENTRY 17,ERROR_CODE 
EXCEPTION_ENTRY 18,NO_ERROR_CODE
EXCEPTION_ENTRY 19,ERROR_CODE
EXCEPTION_ENTRY 20,ERROR_CODE
EXCEPTION_ENTRY 21,NO_ERROR_CODE 
EXCEPTION_ENTRY 22,NO_ERROR_CODE
EXCEPTION_ENTRY 23,ERROR_CODE
EXCEPTION_ENTRY 24,ERROR_CODE
EXCEPTION_ENTRY 25,NO_ERROR_CODE 
EXCEPTION_ENTRY 26,ERROR_CODE
EXCEPTION_ENTRY 27,ERROR_CODE 
EXCEPTION_ENTRY 28,NO_ERROR_CODE
EXCEPTION_ENTRY 29,ERROR_CODE
EXCEPTION_ENTRY 30,ERROR_CODE
EXCEPTION_ENTRY 31,NO_ERROR_CODE 

INTERRUPT_ENTRY 1
INTERRUPT_ENTRY 2
INTERRUPT_ENTRY 3
INTERRUPT_ENTRY 4
INTERRUPT_ENTRY 5
INTERRUPT_ENTRY 6
INTERRUPT_ENTRY 7
INTERRUPT_ENTRY 8
INTERRUPT_ENTRY 9
INTERRUPT_ENTRY 10
INTERRUPT_ENTRY 11
INTERRUPT_ENTRY 12
INTERRUPT_ENTRY 13
INTERRUPT_ENTRY 14
INTERRUPT_ENTRY 15

INTERRUPT_ENTRY	0, 

io_in8:		;int io_in8(int port);
	mov		edx,[esp+4]
	xor		eax,eax
	in		al,dx
	ret

io_in16:	;int io_in16(int port);
	mov		edx,[esp+4]
	xor		eax,eax
	in		ax,dx
	ret
io_in32:	;int io_in32(int port);
	mov		edx,[esp+4]
	in		eax,dx
	ret

io_out8:	; void io_out8(int port, int data);
	mov		edx,[esp+4]
	mov		al,[esp+8]
	out		dx,al
	ret

io_out16:	; void io_out16(int port, int data);
	mov		edx,[esp+4]
	mov		ax,[esp+8]
	out		dx,ax
	ret	

io_out32:	; void io_out32(int port, int data);
	mov		edx,[esp+4]
	mov		eax,[esp+8]
	out		dx,eax
	ret	
	
io_read:	; void io_read(int port, void *buf, int n)
	mov		edx,	[esp +  4]	; edx = port
	mov		edi,	[esp +  8]	; edi = buf
	mov		ecx,	[esp + 12]	; ecx = n
	shr		ecx,	1			; ecx /= 2
	cld
	rep insw					;循环从端口dx(port)读取数据到[es:di](buf) cx(n)次
	ret
	
io_write:	; void io_write(int port, void *buf, int n)
	mov		edx,	[esp +  4]	; edx = port
	mov		edi,	[esp +  8]	; edi = buf
	mov		ecx,	[esp + 12]	; ecx = n
	shr		ecx,	1			; ecx /= 2
	cld
	rep outsw					;循环从[es:di](buf)写数据到端口dx(port) cx(n)次
	ret
	
io_cli:		; void io_cli(void);
	cli
	ret

io_sti:		; void io_sti(void);
	sti
	ret
	
io_hlt: 	;void io_hlt(void);
	hlt
	ret
io_stihlt:
	sti
	hlt
	ret

read_cr3:
	mov eax,	cr3
	ret
	
write_cr3:
	mov	eax,	[esp+4]
	mov	cr3,	eax
	ret
	
read_cr2:
	mov eax,	cr2
	ret

read_cr0:
	mov eax,	cr0
	ret
	
write_cr0:
	mov	eax,	[esp+4]
	mov	cr0,	eax
	ret
	
enable_paging:
	mov	eax,	cr0
	or	eax,	0x80000000
	mov	cr0,	eax
	pop	eax
	add	eax,	0x80000000
	push		eax
	
load_gdtr:	;void load_gdtr(int limit, int addr);
	mov ax, [esp + 4]
	mov	[esp+6],ax		
	lgdt [esp+6]
	
	jmp dword 0x08: .l
	
.l:
	mov ax, 0x10
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	mov ss, ax 
	mov gs, ax 
	ret
	
load_idtr:	;void load_idtr(int limit, int addr);
	mov ax, [esp+4]
	mov	[esp+6],ax
	lidt [esp+6]
	ret
	
io_load_eflags:		; int io_load_eflags(void);
	pushfd			; push eflags
	pop		eax
	ret

io_store_eflags:	; void io_store_eflags(int eflags);
	mov		eax,[esp+4]
	push	eax
	popfd			; pop eflags
	ret

global syscall_handler
syscall_handler:
	push	0
	
	push	ds
	push	es
	push	fs
	push	gs
	pushad
	
	push	0x80
	
	push	edx
	push	ecx
	push	ebx
	push	eax
	
	call 	do_syscall
	add		esp,	16
	
	mov		[esp + 32],	eax
	jmp intr_exit

thread_intr_exit:
	mov esp, [esp + 4]
intr_exit:
	add esp, 4
	popad
	pop gs
	pop fs	
	pop es	 
	pop ds
	add esp, 4
	iret

switch_to:
	push	esi
	push	edi
	push	ebx
	push	ebp

	mov		eax,	[esp + 20]	;获取cur
	mov		[eax],	esp

	mov		eax,	[esp + 24]	;获取next
	mov		esp,	[eax]

	pop		ebp
	pop		ebx
	pop		edi
	pop		esi
	ret