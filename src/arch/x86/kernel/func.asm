	global io_in8,		io_out8,	io_in16,	io_out16,	io_in32,	io_out32
	global io_read,		io_write
	global io_cli,		io_sti,		io_hlt,		io_stihlt
	global read_cr3,	write_cr3,	read_cr0,	write_cr0
	global load_gdtr,	load_idtr
	global io_load_eflags,			io_store_eflags
	global enable_irq
	global divide_error
	global single_step_exception
	global nmi
	global breakpoint_exception
	global stack_exception
	global general_protection
	global page_fault
	global IRQ_timer,	IRQ_pit,	IRQ_keyboard,			IRQ_ide0,	IRQ_ide1
	global switch_to

extern exception_handler
extern irq_table
extern apic_eoi

EOI				equ	0x20
INT_M_CTL		equ	0x20
INT_M_CTLMASK	equ	0x21
INT_S_CTL		equ	0xa0
INT_S_CTLMASK	equ	0xa1

TIMER_IRQ		equ 0
PIT_IRQ			equ 2
KEYBOARD_IRQ	equ	1
IDE0_IRQ		equ 14
IDE1_IRQ		equ 15
[section .text]
[bits 32]

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

read_cr0:
	mov eax,	cr0
	ret
	
write_cr0:
	mov	eax,	[esp+4]
	mov	cr0,	eax
	ret
	
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
	
enable_irq:
	mov		ecx, [esp + 4]          ; irq
	pushf
	cli
	mov		ah, ~1
	rol		ah, cl                  ; ah = ~(1 << (irq % 8))
	cmp		cl, 8
	jae		enable_8                ; enable irq >= 8 at the slave 8259
enable_0:
	in		al, INT_M_CTLMASK
	and		al, ah
	out		INT_M_CTLMASK, al       ; clear bit at master 8259
	popf
	ret
enable_8:
	in		al, INT_S_CTLMASK
	and		al, ah
	out		INT_S_CTLMASK, al       ; clear bit at slave 8259
	popf
	ret
	
divide_error:
	push	0xffffffff
	push	0
	jmp		exception

single_step_exception:
	push	0xffffffff
	push	1
	jmp		exception
	
nmi:
	push	0xffffffff
	push	2
	jmp		exception
	
breakpoint_exception:
	push	0xffffffff
	push	3
	jmp		exception

stack_exception:
	push	12
	jmp		exception

general_protection:
	push	13
	jmp		exception
	
page_fault:
	push	14
	jmp		exception

exception:
	push	esp
	call	exception_handler
	add		esp, 12
	hlt
	iretd

IRQ_timer:
	push ds
	push es
	push fs
	push gs
	pushad
	
	mov dx,ss
	mov ds, dx
	mov es, dx
	
	;mov esp, INTERRUPT_STACK_TOP
	cli
	
	;mov	al, EOI
	;out	INT_M_CTL, al
	call apic_eoi
	
	push TIMER_IRQ
	call [irq_table + TIMER_IRQ*4]
	add esp, 4

	sti
	jmp intr_exit

IRQ_pit:
	push ds
	push es
	push fs
	push gs
	pushad
	
	mov dx,ss
	mov ds, dx
	mov es, dx
	
	;mov esp, INTERRUPT_STACK_TOP
	cli
	
	;mov	al, EOI
	;out	INT_M_CTL, al
	call apic_eoi
	
	push PIT_IRQ
	call [irq_table + PIT_IRQ*4]
	add esp, 4

	sti
	jmp intr_exit
	
IRQ_keyboard:
	push ds
	push es
	push fs
	push gs
	pushad
	
	mov dx,ss
	mov ds, dx
	mov es, dx
	
	;mov esp, INTERRUPT_STACK_TOP
	
	cli 
	;mov	al, EOI
	;out	INT_M_CTL, al
	call apic_eoi
	
	push KEYBOARD_IRQ
	call [irq_table + KEYBOARD_IRQ*4]
	add esp, 4
	
	sti
	jmp intr_exit
	
IRQ_ide0:
	push ds
	push es
	push fs
	push gs
	pushad
	
	mov dx,ss
	mov ds, dx
	mov es, dx
	
	;mov esp, INTERRUPT_STACK_TOP
	
	cli 
	;mov	al, EOI
	;out	INT_M_CTL, al
	call apic_eoi
	
	push IDE0_IRQ
	call [irq_table + IDE0_IRQ*4]
	add esp, 4
	
	sti
	jmp intr_exit
	
IRQ_ide1:
	push ds
	push es
	push fs
	push gs
	pushad
	
	mov dx,ss
	mov ds, dx
	mov es, dx
	
	;mov esp, INTERRUPT_STACK_TOP
	
	cli 
	;mov	al, EOI
	;out	INT_M_CTL, al
	call apic_eoi
	
	push IDE1_IRQ
	call [irq_table + IDE1_IRQ*4]
	add esp, 4
	
	sti
	jmp intr_exit
	
intr_exit:
	popad
	pop gs
	pop fs	
	pop es	 
	pop ds
	iretd

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