	org	0x90000

[bits 16]
align 16

KERNEL_OFF			EQU 10
KERNEL_SEG			EQU 0x1000
BLOCK_SIZE			EQU 128
ARDS_SEG			equ	0x50
ARDS_ZONE_SIZE		equ	0x100
KERNEL_PHY_ADDR		equ KERNEL_SEG*16
KERNEL_START_ADDR	equ 0x100000
LOADER_STACK_TOP	equ	0x90000
VBEINFO_SEG			equ	0x800
X_RESOLUTION		equ	0
Y_RESOLUTION		equ	2
BITS_PER_PIXEL		equ	4
PHYS_BASE_PTR		equ 6
INFO_BLOCK			equ 10
DDC					equ 522
VBEMODE				equ	0x144	;1024*768*24
; 8位
;640*400*8			0x100
;640*480*8			0x101
;800*600*8			0x103
;1024*768*8			0x105
;1280*1024*8		0x107
;1600*1200*8		0x11C
;320*200*8  		0x146
;1152*864*8 		0x148

; 15位
;800*600*15 		0x113
;1024*768*15		0x116
;1280*1024*15		0x119
;1600*1200*15		0x11d
;1152*864*15		0x149

; 16位
;640*480*16 		0x111
;800*600*16 		0x114
;1024*768*16		0x117
;1280*1024*16		0x11A
;1600*1200*16		0x11e
;1152*864*16		0x14a
;1280*768*16		0x175
;1280*800*16		0x178
;1280*960*16		0x17b
;1400*900*16		0x17e
;1400*1050*16		0x181
;1600*900*16		0x193
;1680*1050*16		0x184
;1920*1200*16		0x187
;2560*1600*16		0x18a
;1280*720*16		0x18d
;1920*1080*16		0x190
;2560*1440*16		0x196

; 24位
; 24bit 不能再vmware中使用，不知道为何，可能是不支持
;800*600*24			0x140
;640*480*24 		0x112
;800*600*24 		0x115
;1024*768*24		0x118
;1280*1024*24		0x11B
;1600*1200*24		0x11f
;1152*864*24		0x14b
;1280*768*24		0x176
;1280*800*24		0x179
;1280*960*24		0x17c
;1400*900*24		0x17f
;1400*1050*24		0x182
;1600*900*24		0x194
;1680*1050*24		0x185
;1920*1200*24		0x188
;2560*1600*24		0x18b
;1280*720*24		0x18e
;1920*1080*24		0x191
;2560*1440*24		0x197

; 32位
;320*200*32 		0x140
;640*400*32 		0x141
;640*480*32 		0x142
;800*600*32 		0x143
;1024*768*32		0x144
;1280*1024*32		0x145
;1600*1200*32		0x147
;1152*864*32		0x14c
;1280*768*32		0x177
;1280*800*32		0x17a
;1280*960*32		0x17d
;1400*900*32		0x180
;1400*1050*32		0x183
;1600*900*32		0x195
;1680*1050*32		0x186
;1920*1200*32		0x189
;2560*1600*32		0x18c
;1280*720*32		0x18f
;1920*1080*32		0x192
;2560*1440*32		0x198

	jmp	Start
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

Start:
	mov	ax,	cs
	mov	ds,	ax
	mov	ss,	ax

LoadKernel:
	mov	ax,	KERNEL_SEG
	mov	es,	ax
	mov	ax,	KERNEL_OFF
	mov	cx,	BLOCK_SIZE
	call	LoadBlock
	
	push	ax
	mov	ax,	es
	add	ax,	0x1000
	mov	es,	ax
	pop		ax
	mov	cx,	BLOCK_SIZE
	call	LoadBlock
	
	push	ax
	mov	ax,	es
	add	ax,	0x1000
	mov	es,	ax
	pop		ax
	mov	cx,	BLOCK_SIZE
	call	LoadBlock
	
	call	KillMotor

CheckMemory:
	xor ebx, ebx 
	mov edx, 0x534d4150
	mov di, 0
	mov ax, ARDS_SEG 
	mov es, ax
	mov word [es:ARDS_ZONE_SIZE-4], 0
.E820MemGetLoop:
	mov eax, 0x0000e820
	mov ecx, 20
	int 0x15
	jc .E820CheckFailed
	add di, cx 
	inc word [es:ARDS_ZONE_SIZE-4]
	cmp ebx, 0
	jnz .E820MemGetLoop
	jmp VideoModeSet
.E820CheckFailed:
	jmp $	

LoadBlock:
	xor	bx,	bx
.loop:
	call	ReadOneSector
	add	bx,	512
	inc	ax
	loop	.loop
	ret

VideoModeSet:
;切换到高分辨率
	;是否支持VBE2.0
	mov	ax,	VBEINFO_SEG
	mov	es,	ax
	mov	di,	0
	mov	ax,	0x4f00
	int	0x10
	cmp	ax,	0x004f
	jne	Sleep

	mov	ax,	[es:di + 4]
	cmp	ax,	0x0200
	jb	Sleep
	
	push bx
	mov bx, ds
	
	mov ax, VBEINFO_SEG
	mov ds, ax
	mov eax, [es:di]
	mov [INFO_BLOCK], eax
	
	mov ds, bx
	pop bx
	
	;检查显示模式是否可用
	mov	cx,	VBEMODE
	mov	ax,	0x4f01
	int	0x10
	cmp	ax,	0x004f
	jne	Sleep
	;设置显示模式
	mov	ax,	0x4f02
	mov	bx,	0x4000 + VBEMODE
	int	0x10
	
	mov ax,	VBEINFO_SEG
	mov ds,	ax
	
	xor ax,	ax
	mov	ax,	[es:di+0x12]
	mov	[X_RESOLUTION],		ax	;保存水平分辨率
	mov	ax,	[es:di+0x14]
	mov	[Y_RESOLUTION],		ax	;保存垂直分辨率
	mov	al,	[es:di+0x19]
	mov	[BITS_PER_PIXEL],	al	;保存像素位宽
	mov	eax,[es:di+0x28]
	mov	[PHYS_BASE_PTR],	eax	;保存vram起始地址
	
	push cx
	mov ax, 0x4f15
	mov bl, 0x01
	mov cx, 0x00
	mov dx, 0x00
	mov di, DDC
	int 0x10
	pop cx
	
	mov	ax,	VBEINFO_SEG
	mov	es,	ax
	mov	di,	INFO_BLOCK
	mov	ax,	0x4f00
	int	0x10

	mov	ax,	cs
	mov	ds,	ax

;进入保护模式
SetProtectMode:
	;禁止中断
	cli
	;开启A20地址线
	in	al,		0x92
	or	al,		2
	out	0x92,	al
	;加载GDTR
	lgdt	[GDTR]
	;置位CR0.PE，进入保护模式
	mov	eax,	cr0
	or	eax,	1
	mov	cr0,	eax
	;通过JMP使cs进入32位模式
	jmp	dword	0x08:Flush

;读取软盘
ReadOneSector:
	push	ax
	push	cx
	;LBA转CHS
	mov	dl,	36
	div	dl
	mov	ch,	al		;CH=(AX/36)

	mov	al,	ah
	mov	ah,	0
	mov	dl,	18
	div	dl
	mov	dh,	al		;DH=(AX%36)/18
	mov	cl,	ah		;CL=(AX%36)%18
	inc	cl			;CL+=1

	mov	dl,	0		;DL=0
	;调用BIOS中断
	mov	ax,	0x0201	;AH=0x02,AL=0x01

retry:
	int	0x13
	jc	retry
	pop		cx
	pop		ax

	ret


KillMotor:
	push	dx
	mov	dx,	0x03f2
	mov	al,	0
	out	dx,	al
	pop	dx
	ret

Sleep:
	hlt
	jmp	$

[bits 32]
align 32
Flush:
	;初始化段寄存器
	mov	ax,		0x10
	mov	ds,		ax
	mov	es,		ax
	mov	fs,		ax
	mov	gs,		ax
	mov	ss,		ax
	mov	esp,	LOADER_STACK_TOP

	call	ReadKernel
	
	jmp 0x08:KERNEL_START_ADDR
	
	push eax
	jmp $

ReadKernel:
;File header
;typedef struct elf_hdr_s {
;0x00    uint32 magic;  		;ELF文件头
;0x04   -uint8  elf[12];
;0x05	 	uint8	class		;文件类型，01:32位程序	02:64位程序
;0x06		uint8	data		;数据编码，01:高位在前	02:低位在前
;0x07		uint8	version		;文件版本，固定01
;0x08		uint8	pad[5]		;?
;0x0f		uint8	nident		;?
;0x10    uint16 type;			;文件类型，01:未知文件类型	02:可重定位文件类型	03:可执行文件	04:共享目标文件	...
;0x12    uint16 machine;		;声明ABI，03:x86	28:arm
;0x14    uint32 version;
;0x18    uint32 entry;			;可执行程序入口点地址
;0x1c    uint32 phoff;			;程序头部索引地址
;0x20    uint32 shoff;			;节区表索引地址
;0x24    uint32 flags;
;0x28    uint16 ehsize;			;elf头部的大小
;0x2a    uint16 phentsize;		;程序头部表的单个表项大小
;0x2c    uint16 phnum;			;程序头部表的表项数
;0x2e    uint16 shentsize;		;节区表的单个表项大小
;0x30    uint16 shnum;			;节区表的表项数
;0x32    uint16 shstrndx;
;} elf_hdr_t;
;
;Program section header
;typedef struct prog_hdr_s {
;0x00    uint32 type;			;声明此段的作用类型
;								;00		此数组元素未用。结构中其他成员都是未定义的
;								;01		此数组元素给出一个可加载的段,段的大小由 
;								;		p_filesz 和 p_memsz 描述。文件中的字
;								;		节被映射到内存段开始处。如果 p_memsz 
;								;		大于 p_filesz,“剩余”的字节要清零。
;								;		p_filesz 不能大于 p_memsz。可加载的段
;								;		在程序头部表格中根据 p_vaddr 成员按升序排列
;								;02		数组元素给出动态链接信息
;								;03		数组元素给出一个 NULL 结尾的字符串的位置和长
;								;		度,该字符串将被当作解释器调用。这种段类型仅对
;								;		与可执行文件有意义(尽管也可能在共享目标文件上
;								;		发生)。在一个文件中不能出现一次以上。如果存在
;								;		这种类型的段,它必须在所有可加载段项目的前面
;								;04		此数组元素给出附加信息的位置和大小
;								;05		此段类型被保留,不过语义未指定。包含这种类型的
;								;		段的程序与 ABI不符
;								;06		此类型的数组元素如果存在,则给出了程序头部表自
;								;		身的大小和位置,既包括在文件中也包括在内存中的
;								;		信息。此类型的段在文件中不能出现一次以上。并且
;								;		只有程序头部表是程序的内存映像的一部分时才起作
;								;		用。如果存在此类型段,则必须在所有可加载段项目
;								;		的前面
;0x04    uint32 off;			;段相对于文件的索引地址
;0x08    uint32 vaddr;			;段在内存中的虚拟地址
;0x0c    uint32 paddr;			;段的物理地址
;0x10    uint32 filesz;			;段在文件中所占的长度
;0x14    uint32 memsz;			;段在内存中所占的长度
;0x18    uint32 flags;			;段相关标志(read、write、exec)
;0x1c    uint32 align;			;字节对齐
;} prog_hdr_t;
	xor		esi,	esi
	mov		cx,		word	[KERNEL_PHY_ADDR + 0x2c]	;获取程序头部表的表项数
	movzx	ecx,	cx
	mov		esi,	[KERNEL_PHY_ADDR + 0x1c]			;获取程序头部索引地址
	add		esi,	KERNEL_PHY_ADDR
.begin:
	mov		eax,	[esi]
	cmp		eax,	1
	jne		.unaction
	push	dword	[esi + 0x10]	;压入段大小
	mov		eax,	[esi + 0x04]	;获取段的地址
	add		eax,	KERNEL_PHY_ADDR
	push	eax						;压入段的地址
	push	dword	[esi + 0x08]	;压入段的虚拟地址
	call	memcpy					;复制内存
	add		esp,	12

.unaction:
	add		esi,	0x20		;查找下一个表项
	dec		ecx
	jnz		.begin
	ret

memcpy:
	push	ebp
	mov		ebp,	esp

	push	esi
	push	edi
	push	ecx

	mov		edi,	[ebp + 8]		;目标地址
	mov		esi,	[ebp + 12]		;源地址
	mov		ecx,	[ebp + 16]		;大小
.1:
	cmp		ecx,	0
	jz		.2

	mov		al,		[ds:esi]
	inc		esi
	mov		byte	[es:edi],	al
	inc		edi

	dec		ecx
	jmp		.1
.2:
	mov		eax,	[ebp + 8]

	pop		ecx
	pop		edi
	pop		esi
	mov		esp,	ebp
	pop		ebp

	ret

	jmp $

	times	4096 - ($ - $$)	db	0