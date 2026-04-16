	global io_in8,		io_out8,	io_in16,	io_out16,	io_in32,	io_out32
	global io_ins8,		io_outs8,	io_ins16,	io_outs16,	io_ins32,	io_outs32
	global io_cli,		io_sti,		io_hlt,		io_stihlt,	save_eflags_cli
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
global IRQ_timer,	IRQ_pit,	IRQ_keyboard
global thread_intr_exit, kernel_thread_entry
global switch_to

extern exception_handler
extern irq_table
extern do_irq
extern do_syscall
extern apic_eoi
extern kernel_thread

EOI				equ	0x20
INT_M_CTL		equ	0x20
INT_M_CTLMASK	equ	0x21
INT_S_CTL		equ	0xa0
INT_S_CTLMASK	equ	0xa1

%define   ERROR_CODE   nop
%define   NO_ERROR_CODE push -1

%macro CALL_C_ALIGNED 1
	mov rax, rsp
	and rsp, -16
	sub rsp, 16
	mov [rsp], rax
	cld
	call %1
	mov rsp, [rsp]
%endmacro

%macro INTERRUPT_ENTRY 1
global irq_entry%1
irq_entry%1:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi
	push rbp
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15
	
	mov rdi, %1
	mov rsi, rsp
	CALL_C_ALIGNED do_irq
	
	jmp irq_exit

%endmacro

%macro EXCEPTION_ENTRY 2
global exception_entry%1
exception_entry%1:
    %2
	mov  rdi, %1
	mov  rsi, rsp
    push rax
    mov  rax, [rsp + 16]
    push rax
    push rbp
    mov  rbp, rsp
    CALL_C_ALIGNED exception_handler
    
    hlt
%endmacro

[section .text]
[bits 64]

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
EXCEPTION_ENTRY 12,ERROR_CODE
EXCEPTION_ENTRY 13,ERROR_CODE
EXCEPTION_ENTRY 14,ERROR_CODE
EXCEPTION_ENTRY 15,NO_ERROR_CODE 
EXCEPTION_ENTRY 16,NO_ERROR_CODE
EXCEPTION_ENTRY 17,ERROR_CODE 
EXCEPTION_ENTRY 18,NO_ERROR_CODE
EXCEPTION_ENTRY 19,NO_ERROR_CODE
EXCEPTION_ENTRY 20,NO_ERROR_CODE
EXCEPTION_ENTRY 21,ERROR_CODE 
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

INTERRUPT_ENTRY	0

io_in8:     ; port in rdi
	mov    dx, di
	xor    eax, eax
	in     al, dx
	ret

io_in16:
	mov    dx, di
	xor    eax, eax
	in     ax, dx
	ret

io_in32:
	mov    dx, di
	in     eax, dx
	ret

io_out8:
	mov    dx, di
	mov    al, sil
	out    dx, al
	ret

io_out16:
	mov    dx, di
	mov    ax, si
	out    dx, ax
	ret

io_out32:
	mov    dx, di
	mov    eax, esi
	out    dx, eax
	ret
	
io_ins8:
    mov     rcx, rdx
	mov     dx, di
	mov		rdi, rsi
    cld
    rep     insb
    ret

io_ins16:
    mov     rcx, rdx
	mov     dx, di
	mov		rdi, rsi
    cld
    rep     insw
    ret

io_ins32:
    mov     rcx, rdx
	mov     dx, di
	mov		rdi, rsi
    cld
    rep     insd
    ret

io_outs8:
    mov     rcx, rdx
    mov     dx, di
    cld
    rep     outsb
    ret

io_outs16:
    mov     rcx, rdx
    mov     dx, di
    cld
    rep     outsw
    ret

io_outs32:
    mov     rcx, rdx
    mov     dx, di
    cld
    rep     outsd
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
	mov    rax, cr3
	ret
	
write_cr3:
	mov    cr3, rdi
	ret
	
read_cr2:
	mov    rax, cr2
	ret

read_cr0:
	mov    rax, cr0
	ret
	
write_cr0:
	mov    cr0, rdi
	ret
	
_GDTR:
	dw 0
	dq 0

;void load_gdtr(uint16_t limit, uint64_t addr)
load_gdtr:
	mov	[_GDTR], di
	mov	[_GDTR + 2], rsi
	lgdt   [_GDTR]
	ret
	
_IDTR:
	dw 0
	dq 0

;void load_idtr(uint16_t limit, uint64_t addr)
load_idtr:
	mov	[_IDTR], di
	mov	[_IDTR + 2], rsi
	lidt   [_IDTR]
	ret
	
io_load_eflags:
	pushfq
	pop    rax
	ret

io_store_eflags:
	push   rdi
	popfq
	ret

save_eflags_cli:
	pushfq
	cli
	pop    rax
	ret

global syscall_handler
syscall_handler:
	push	0
	
	push	rax
	push	rbx
	push	rcx
	push	rdx
	push	rsi
	push	rdi
	push	rbp
	push	r8
	push	r9
	push	r10
	push	r11
	push	r12
	push	r13
	push	r14
	push	r15
	
	mov	rdi, 0x80
	mov	rsi, rsp
	CALL_C_ALIGNED do_syscall
	
	mov	[rsp + 120], rax
irq_exit:
	pop    r15
	pop    r14
	pop    r13
	pop    r12
	pop    r11
	pop    r10
	pop    r9
	pop    r8
	pop    rbp
	pop    rdi
	pop    rsi
	pop    rdx
	pop    rcx
	pop    rbx
	pop    rax
	iretq
thread_intr_exit:
intr_exit:
	pop    r15
	pop    r14
	pop    r13
	pop    r12
	pop    r11
	pop    r10
	pop    r9
	pop    r8
	pop    rbp
	pop    rdi
	pop    rsi
	pop    rdx
	pop    rcx
	pop    rbx
	pop    rax
	add    rsp, 8
	iretq
switch_to:
	push	rbp
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	
	mov	[rdi], rsp
	mov	rsp, [rsi]
	
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	pop	rbp
	ret

kernel_thread_entry:
	mov	rdi, r12
	mov	rsi, r13
	call	kernel_thread
	hlt
