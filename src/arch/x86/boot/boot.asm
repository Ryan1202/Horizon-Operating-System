	org	0x7c00

LOADER_SEG	equ	0x9000
LOADER_OFF	equ	2
LOADER_CNTS	equ	8

Start:
	mov	ax,	cs
	mov	ds,	ax
	mov	es, ax
;清屏
	mov	ax,	0x0600	;ah=0x06,al=0x00
	mov	bx,	0x0700	;bh=0x07(黑底白字)
	mov	cx,	0x0000	;ch=0,cl=0
	mov	dx,	0x184f	;dh=24,dl=79
	int	0x10
;设置光标位置
	mov	ax,	0x0200	;ah=0x02
	mov	bx,	0x0000	;bh=0x00
	mov	dx,	0x0000	;ch=0,cl=0
	int	0x10
;输出“Booting...“
	mov	ax, 0x1301	;ah=0x13,al=0x01
	mov	bx, 0x0007	;bh=0x00,bl=0x07(黑底白字)
	mov cx,	0x000a	;cx=10(字符串长度)
	mov	dx,	0x0000	;dh=0,dl=0
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop		ax
	mov	bp,	BootMessage
	int	0x10
;软盘复位
	mov	ax,	0x0000	;ah=0x00
	mov	dx,	0x0000	;dl=0x00(软盘A)
	int 0x13

	jmp	FindLoader
;读取软盘
;===============
;AX:LBA地址
;ES:BX:数据地址
;===============
;BIOS读取磁盘
;int 0x13
;AH=02H
;AL=需要读入的扇区数
;CH=柱面号
;CL=扇区号(bit 0~5),柱面号高2位(bit 6~7)
;DH=磁头号
;DL=驱动器号
;ES:BX：数据缓冲区
;===============
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

FindLoader:
	mov	ax,	LOADER_SEG
	mov	es,	ax
	xor	bx,	bx
	mov	ax,	LOADER_OFF
	mov	cx,	LOADER_CNTS
ReadLoader:
	call	ReadOneSector
	add	bx,	512
	inc	ax
	loop	ReadLoader
	
	jmp	LOADER_SEG:0

	jmp $

BootMessage	db	"Booting..."
	times 510 - ($ - $$)	db	0
	dw	0xaa55
