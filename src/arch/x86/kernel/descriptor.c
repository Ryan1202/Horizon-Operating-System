/**
 * @file descriptor.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 配置GDT、IDT等描述符
 * @version 1.2
 * @date 2022-07-31
 */
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/page.h>
#include <kernel/tss.h>
#include <string.h>

irq_handler_t	irq_table[NR_IRQ];
void			default_irq_handler(int irq);
extern uint32_t syscall_handler(void);

struct segment_descriptor *gdt;
struct gate_descriptor	  *idt;
static struct tss_s		   tss;

/**
 * @brief 更新TSS中的ESP为对应任务的ESP
 *
 * @param pthread 任务结构
 */
void update_tss_esp(struct task_s *pthread) {
	tss.esp0 = (uint32_t *)((uint32_t)pthread + PAGE_SIZE);
}

/**
 * @brief 初始化描述符
 *
 */
void init_descriptor(void) {
	gdt = (struct segment_descriptor *)GDT_ADDR;
	idt = (struct gate_descriptor *)IDT_ADDR;
	memset(&tss, 0, sizeof(struct tss_s));
	tss.ss0		= SELECTOR_K_STACK;
	tss.io_base = sizeof(struct tss_s);

	// 配置GDT
	int i;
	for (i = 0; i < GDT_SIZE / 8; i++) {
		set_segmdesc(gdt + i, 0, 0, 0);
	}
	// 内核代码段
	set_segmdesc(gdt + 1, 0xffffffff, 0x00000000,
				 DESC_D | DESC_P | DESC_S_CODE | DESC_TYPE_CODE | DESC_DPL_0);
	// 内核数据段
	set_segmdesc(gdt + 2, 0xffffffff, 0x00000000,
				 DESC_D | DESC_P | DESC_S_DATA | DESC_TYPE_DATA | DESC_DPL_0);
	// 用户代码段
	set_segmdesc(gdt + 3, 0xffffffff, 0x00000000,
				 DESC_D | DESC_P | DESC_S_CODE | DESC_TYPE_CODE | DESC_DPL_3);
	// 用户数据段
	set_segmdesc(gdt + 4, 0xffffffff, 0x00000000,
				 DESC_D | DESC_P | DESC_S_DATA | DESC_TYPE_DATA | DESC_DPL_3);
	// TSS
	set_segmdesc(gdt + 5, sizeof(struct tss_s), (int)&tss,
				 DESC_D | DESC_P | DESC_S_SYS | DESC_TYPE_TSS | DESC_DPL_0);

	// 改变GDTR寄存器使其指向刚配置好的GDT
	load_gdtr(GDT_SIZE, GDT_ADDR);
	__asm__ __volatile__("ltr %w0" ::"r"(SElECTOR_TSS));

	// 配置IDT
	// 0x00-0x1f号中断是CPU异常中断, 0x20-0x2f号中断是IRQ中断
	for (i = 0; i < IDT_SIZE / 8; i++) {
		set_gatedesc(idt + i, 0, 0, 0);
	}
	set_gatedesc(idt + 0x00, (int)&exception_entry0, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x01, (int)&exception_entry1, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x02, (int)&exception_entry2, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x03, (int)&exception_entry3, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x04, (int)&exception_entry4, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x05, (int)&exception_entry5, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x06, (int)&exception_entry6, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x07, (int)&exception_entry7, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x08, (int)&exception_entry8, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x09, (int)&exception_entry9, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x0a, (int)&exception_entry10, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x0b, (int)&exception_entry11, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x0c, (int)&exception_entry12, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x0d, (int)&exception_entry13, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x0e, (int)&exception_entry14, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x0f, (int)&exception_entry15, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x10, (int)&exception_entry16, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x11, (int)&exception_entry17, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x12, (int)&exception_entry18, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x13, (int)&exception_entry19, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x14, (int)&exception_entry20, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x15, (int)&exception_entry21, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x16, (int)&exception_entry22, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x17, (int)&exception_entry23, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x18, (int)&exception_entry24, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x19, (int)&exception_entry25, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x1a, (int)&exception_entry26, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x1b, (int)&exception_entry27, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x1c, (int)&exception_entry28, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x1d, (int)&exception_entry29, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x1e, (int)&exception_entry30, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x1f, (int)&exception_entry31, 0x08, DA_386IGate_DPL0);

	set_gatedesc(idt + 0x20 + 0, (int)&irq_entry0, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 1, (int)&irq_entry1, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 2, (int)&irq_entry2, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 3, (int)&irq_entry3, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 4, (int)&irq_entry4, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 5, (int)&irq_entry5, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 6, (int)&irq_entry6, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 7, (int)&irq_entry7, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 8, (int)&irq_entry8, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 9, (int)&irq_entry9, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 10, (int)&irq_entry10, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 11, (int)&irq_entry11, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 12, (int)&irq_entry12, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 13, (int)&irq_entry13, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 14, (int)&irq_entry14, 0x08, DA_386IGate_DPL0);
	set_gatedesc(idt + 0x20 + 15, (int)&irq_entry15, 0x08, DA_386IGate_DPL0);
	for (i = 0; i < NR_IRQ; i++) {
		irq_table[i] = default_irq_handler;
	}

	set_gatedesc(idt + 0x80, (int)syscall_handler, 0x08, DA_386IGate_DPL3);

	load_idtr(IDT_SIZE, IDT_ADDR);
}

/**
 * @brief 设置IRQ中断处理函数
 *
 * @param irq IRQ号
 * @param handler 中断处理函数
 */
void put_irq_handler(int irq, irq_handler_t handler) {
	irq_table[irq] = handler;
}

/**
 * @brief 创建段描述符
 *
 * @param sd 段描述符结构
 * @param limit 段长度
 * @param base 段起始地址
 * @param ar 标志
 */
void set_segmdesc(struct segment_descriptor *sd, unsigned int limit, int base, int ar) {
	if (limit > 0xfffff) {
		ar |= 0x8000; /* G_bit = 1 */
		limit /= 0x1000;
	}

	sd->limit_low	 = limit & 0xffff;
	sd->base_low	 = base & 0xffff;
	sd->base_mid	 = (base >> 16) & 0xff;
	sd->access_right = ar & 0xff;
	sd->limit_high	 = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0);
	sd->base_high	 = (base >> 24) & 0xff;
	return;
}

/*
 *                                   任务门
 * 31                                16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |                                   | P | DPL | 00101 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |            TSS段选择子            |                                   |
 * -------------------------------------------------------------------------
 *
 *                                   中断门
 * 31                               16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |            偏移 31:16             | P | DPL | 0D110 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |             段选择子              |             偏移 15:0             |
 * -------------------------------------------------------------------------
 *
 *                                   陷阱门
 * 31                                16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |            偏移 31:16             | P | DPL | 0D111 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |             段选择子              |             偏移 15:0             |
 * -------------------------------------------------------------------------
 * D为0表示16位，D为1表示32位
 */

/**
 * @brief 创建门描述符
 *
 * @param gd 门描述符结构
 * @param offset 偏移地址
 * @param selector 段选择子
 * @param ar 标志
 */
void set_gatedesc(struct gate_descriptor *gd, int offset, int selector, int ar) {
	gd->offset_low	 = offset & 0xffff;
	gd->selector	 = selector;
	gd->dw_count	 = (ar >> 8) & 0xff;
	gd->access_right = ar & 0xff;
	gd->offset_high	 = (offset >> 16) & 0xffff;
	return;
}

/**
 * @brief 异常中断处理程序
 *
 * @param esp
 * @param vec_no
 * @param err_code
 * @param eip
 * @param cs
 * @param eflags
 */
void exception_handler(int esp, int vec_no, int err_code, int eip, int cs, int eflags) {
	char err_description[][64] = {"#DE Divide Error",
								  "#DB RESERVED",
								  "NMI Interrupt",
								  "#BP Breakpoint",
								  "#OF Overflow",
								  "#BR BOUND Range Exceeded",
								  "#UD Invalid Opcode (Undefined Opcode)",
								  "#NM Device Not Available (No Math Coprocessor)",
								  "#DF Double Fault",
								  "    Coprocessor Segment Overrun (reserved)",
								  "#TS Invalid TSS",
								  "#NP Segment Not Present",
								  "#SS Stack-Segment Fault",
								  "#GP General Protection",
								  "#PF Page Fault",
								  "(Intel reserved. Do not use.)",
								  "#MF x87 FPU Floating-Point Error (Math Fault)",
								  "#AC Alignment Check",
								  "#MC Machine Check",
								  "#XF SIMD Floating-Point Exception"};
	io_cli();

	printk(COLOR_RED "ERROR:%s\n", err_description[vec_no]);

	printk(COLOR_RED "EFLAGS:%x CS:%x EIP:%x ESP:%x\n", eflags, cs, eip, esp);

	if (vec_no == 14) { printk(COLOR_RED "Address:%#08x\n", read_cr2()); }

	if (err_code != 0xFFFFFFFF) {
		printk(COLOR_RED "Error code:%x\n", err_code);

		if (err_code & 1) {
			printk(COLOR_RED "    External Event: NMI,hard interruption,ect.\n");
		} else {
			printk(COLOR_RED "    Not External Event: inside.\n");
		}
		if (err_code & (1 << 1)) {
			printk(COLOR_RED "    IDT: selector in idt.\n");
		} else {
			printk(COLOR_RED "    IDT: selector in gdt or ldt.\n");
		}
		if (err_code & (1 << 2)) {
			printk(COLOR_RED "    TI: selector in ldt.\n");
		} else {
			printk(COLOR_RED "    TI: selector in gdt.\n");
		}
		printk(COLOR_RED "    Selector: idx %d\n", (err_code & 0xfff8) >> 3);
	}

	io_hlt();
	while (1)
		;
}

void do_irq(int irq) {
	if (use_apic) {
		apic_eoi();
	} else {
		pic_eoi(irq);
	}
	device_irq_handler(irq);
	irq_table[irq](irq);
}

void default_irq_handler(int irq) {
	if (use_apic) {
		apic_eoi();
	} else {
		pic_eoi(irq);
	}
}

void irq_enable(int irq) {
	if (use_apic) {
		apic_enable_irq(irq);
	} else {
		pic_enable_irq(irq);
	}
}
