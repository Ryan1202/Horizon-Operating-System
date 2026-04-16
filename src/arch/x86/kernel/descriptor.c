/**
 * @file descriptor.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 配置GDT、IDT等描述符
 * @version 1.2
 * @date 2022-07-31
 */
#include "kernel/platform.h"
#include <driver/interrupt/interrupt_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/page.h>
#include <kernel/softirq.h>
#include <kernel/thread.h>
#include <kernel/tss.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GDT_ENTRY_TSS			5
#define GDT_ENTRY_PERCPU_BASE	7
#define EXCEPTION_NO_ERROR_CODE ((size_t)-1)

#define SET_EXCEPTION_ENTRY(n) \
	set_gate_descriptor(       \
		&idt[n], (size_t)&exception_entry##n, 0x08, DA_386IGate_DPL0);

#define SET_IRQ_ENTRY(n) \
	set_gate_descriptor( \
		&idt[0x20 + n], (size_t)&irq_entry##n, 0x08, DA_386IGate_DPL0);

irq_handler_t irq_table[NR_IRQ];
void		  default_irq_handler(int irq);
extern void	  syscall_handler(void);

struct segment_descriptor *gdt;
struct gate_descriptor	  *idt;
static struct tss_s		   tss;

static void set_tss_descriptor(
	int entry, uint32_t limit, uint64_t base, uint8_t access_right) {
	uint64_t *descriptor = (uint64_t *)(gdt + entry);
	uint64_t  low		 = 0;
	uint64_t  high		 = 0;

	low |= (uint64_t)(limit & 0xffff);
	low |= (uint64_t)(base & 0xffff) << 16;
	low |= (uint64_t)((base >> 16) & 0xff) << 32;
	low |= (uint64_t)access_right << 40;
	low |= (uint64_t)((limit >> 16) & 0x0f) << 48;
	low |= (uint64_t)((base >> 24) & 0xff) << 56;
	high |= (base >> 32) & 0xffffffff;

	descriptor[0] = low;
	descriptor[1] = high;
}

/**
 * @brief 更新TSS中的ESP为对应任务的ESP
 *
 * @param pthread 任务结构
 */
void update_tss_esp(struct task_s *pthread) {
	tss.rsp[0] = ((uint64_t)pthread + PAGE_SIZE);
}

/**
 * @brief 初始化描述符
 *
 */
void init_descriptor(void) {
	gdt = (struct segment_descriptor *)GDT_BASE;
	idt = (struct gate_descriptor *)IDT_BASE;
	memset(&tss, 0, sizeof(struct tss_s));
	tss.io_base = sizeof(struct tss_s);

	// 配置GDT
	int i;
	for (i = 0; i < (GDT_SIZE + 1) / 8; i++) {
		set_segment_descriptor(gdt + i, 0, 0, 0);
	}
	// 内核代码段
	set_segment_descriptor(
		gdt + 1, 0xffffffff, 0x00000000,
		DESC_L | DESC_P | DESC_S | DESC_TYPE_CODE | DESC_DPL(0));
	// 内核数据段
	set_segment_descriptor(
		gdt + 2, 0xffffffff, 0x00000000,
		DESC_P | DESC_S | DESC_TYPE_DATA | DESC_DPL(0));
	// 用户代码段
	set_segment_descriptor(
		gdt + 3, 0xffffffff, 0x00000000,
		DESC_L | DESC_P | DESC_S | DESC_TYPE_CODE | DESC_DPL(3));
	// 用户数据段
	set_segment_descriptor(
		gdt + 4, 0xffffffff, 0x00000000,
		DESC_P | DESC_S | DESC_TYPE_DATA | DESC_DPL(3));
	// TSS
	set_tss_descriptor(
		GDT_ENTRY_TSS, (uint32_t)(sizeof(struct tss_s) - 1),
		(uint64_t)(size_t)&tss, DESC_P | DESC_TYPE_TSS | DESC_DPL(0));

	// 改变GDTR寄存器使其指向刚配置好的GDT
	load_gdtr((uint16_t)GDT_SIZE, (size_t)gdt);
	__asm__ __volatile__("ltr %w0" ::"r"(SELECTOR_TSS));

	// 配置IDT
	// 0x00-0x1f号中断是CPU异常中断, 0x20-0x2f号中断是IRQ中断
	for (i = 0; i < (IDT_SIZE + 1) / (int)sizeof(struct gate_descriptor); i++) {
		set_gate_descriptor(idt + i, 0, 0, 0);
	}

	SET_EXCEPTION_ENTRY(0)
	SET_EXCEPTION_ENTRY(1)
	SET_EXCEPTION_ENTRY(2)
	SET_EXCEPTION_ENTRY(3)
	SET_EXCEPTION_ENTRY(4)
	SET_EXCEPTION_ENTRY(5)
	SET_EXCEPTION_ENTRY(6)
	SET_EXCEPTION_ENTRY(7)
	SET_EXCEPTION_ENTRY(8)
	SET_EXCEPTION_ENTRY(9)
	SET_EXCEPTION_ENTRY(10)
	SET_EXCEPTION_ENTRY(11)
	SET_EXCEPTION_ENTRY(12)
	SET_EXCEPTION_ENTRY(13)
	SET_EXCEPTION_ENTRY(14)
	SET_EXCEPTION_ENTRY(15)
	SET_EXCEPTION_ENTRY(16)
	SET_EXCEPTION_ENTRY(17)
	SET_EXCEPTION_ENTRY(18)
	SET_EXCEPTION_ENTRY(19)
	SET_EXCEPTION_ENTRY(20)
	SET_EXCEPTION_ENTRY(21)
	SET_EXCEPTION_ENTRY(22)
	SET_EXCEPTION_ENTRY(23)
	SET_EXCEPTION_ENTRY(24)
	SET_EXCEPTION_ENTRY(25)
	SET_EXCEPTION_ENTRY(26)
	SET_EXCEPTION_ENTRY(27)
	SET_EXCEPTION_ENTRY(28)
	SET_EXCEPTION_ENTRY(29)
	SET_EXCEPTION_ENTRY(30)
	SET_EXCEPTION_ENTRY(31)

	SET_IRQ_ENTRY(0)
	SET_IRQ_ENTRY(1)
	SET_IRQ_ENTRY(2)
	SET_IRQ_ENTRY(3)
	SET_IRQ_ENTRY(4)
	SET_IRQ_ENTRY(5)
	SET_IRQ_ENTRY(6)
	SET_IRQ_ENTRY(7)
	SET_IRQ_ENTRY(8)
	SET_IRQ_ENTRY(9)
	SET_IRQ_ENTRY(10)
	SET_IRQ_ENTRY(11)
	SET_IRQ_ENTRY(12)
	SET_IRQ_ENTRY(13)
	SET_IRQ_ENTRY(14)
	SET_IRQ_ENTRY(15)

	for (i = 0; i < NR_IRQ; i++) {
		irq_table[i] = default_irq_handler;
	}

	set_gate_descriptor(
		&idt[0x80], (size_t)syscall_handler, 0x08, DA_386IGate_DPL3);

	load_idtr((uint16_t)IDT_SIZE, (size_t)idt);
}

uint16_t set_percpu_segment_descriptor(int cpu_id, size_t addr) {
	set_segment_descriptor(
		gdt + GDT_ENTRY_PERCPU_BASE + cpu_id, 0xffffffff, addr,
		DESC_L | DESC_P | DESC_S | DESC_TYPE_DATA | DESC_DPL(0));
	return (GDT_ENTRY_PERCPU_BASE + cpu_id) * 0x8;
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
void set_segment_descriptor(
	struct segment_descriptor *sd, uint32_t limit, uint64_t base, int ar) {
	uint32_t base32 = (uint32_t)base;

	// if (limit > 0xfffff) {
	// 	ar |= 0x8000; /* G_bit = 1 */
	// 	limit /= 0x1000;
	// }

	sd->limit_low	 = limit & 0xffff;
	sd->base_low	 = base32 & 0xffff;
	sd->base_mid	 = (base32 >> 16) & 0xff;
	sd->access_right = ar & 0xff;
	sd->limit_high	 = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0);
	sd->base_high	 = (base32 >> 24) & 0xff;
	return;
}

/*
 *                                   任务门
 * 31                                16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |                                   | P | DPL | 00101 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |            TSS段选择子             |                                   |
 * -------------------------------------------------------------------------
 *
 *                                   中断门
 * 31                               16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |            偏移 31:16             | P | DPL | 0D110 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |             段选择子               |             偏移 15:0             |
 * -------------------------------------------------------------------------
 *
 *                                   陷阱门
 * 31                                16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |            偏移 31:16             | P | DPL | 0D111 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |             段选择子               |             偏移 15:0             |
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
void set_gate_descriptor(
	struct gate_descriptor *gd, uint64_t offset, uint16_t selector,
	uint8_t ar) {
	gd->offset_low	 = offset & 0xffff;
	gd->selector	 = selector;
	gd->ist			 = 0;
	gd->access_right = ar;
	gd->offset_mid	 = (offset >> 16) & 0xffff;
	gd->offset_high	 = (offset >> 32) & 0xffffffff;
	gd->reserved	 = 0;
	return;
}

/**
 * @brief 异常中断处理程序
 *
 * @param vec_no
 * @param stack_frame
 */
void exception_handler(int vec_no, uint64_t *stack_frame) {
	static const char *const err_description[] = {
		"#DE Divide Error",
		"#DB Debug",
		"NMI Interrupt",
		"#BP Breakpoint",
		"#OF Overflow",
		"#BR BOUND Range Exceeded",
		"#UD Invalid Opcode",
		"#NM Device Not Available",
		"#DF Double Fault",
		"Reserved",
		"#TS Invalid TSS",
		"#NP Segment Not Present",
		"#SS Stack-Segment Fault",
		"#GP General Protection",
		"#PF Page Fault",
		"Reserved",
		"#MF x87 Floating-Point Exception",
		"#AC Alignment Check",
		"#MC Machine Check",
		"#XM SIMD Floating-Point Exception",
		"#VE Virtualization Exception",
		"#CP Control Protection Exception",
		"Reserved",
		"Reserved",
		"Reserved",
		"Reserved",
		"Reserved",
		"Reserved",
		"#HV Hypervisor Injection Exception",
		"#VC VMM Communication Exception",
		"#SX Security Exception",
		"Reserved"};
	uint64_t	error_code	  = stack_frame[0];
	uint64_t	rip			  = stack_frame[1];
	uint64_t	cs			  = stack_frame[2];
	uint64_t	rflags		  = stack_frame[3];
	const char *description	  = "Unknown Exception";
	int			has_saved_rsp = (cs & 0x3) == 3;

	if (vec_no >= 0 &&
		vec_no < (int)(sizeof(err_description) / sizeof(err_description[0]))) {
		description = err_description[vec_no];
	}

	io_cli();

	printk(COLOR_RED "ERROR:%s\n", description);
	printk(
		COLOR_RED "RFLAGS:%#018lx CS:%#lx RIP:%#018lx\n", (unsigned long)rflags,
		(unsigned long)cs, (unsigned long)rip);

	if (has_saved_rsp) {
		printk(
			COLOR_RED "RSP:%#018lx SS:%#lx\n", (unsigned long)stack_frame[4],
			(unsigned long)stack_frame[5]);
	}

	if (vec_no == 14) {
		printk(COLOR_RED "CR2:%#018lx\n", (unsigned long)read_cr2());
	}

	if (error_code != EXCEPTION_NO_ERROR_CODE) {
		printk(COLOR_RED "Error code:%#lx\n", (unsigned long)error_code);

		if (error_code & 1) {
			printk(COLOR_RED
				   "    External Event: NMI,hard interruption,ect.\n");
		} else {
			printk(COLOR_RED "    Not External Event: inside.\n");
		}
		if (error_code & (1 << 1)) {
			printk(COLOR_RED "    IDT: selector in idt.\n");
		} else {
			printk(COLOR_RED "    IDT: selector in gdt or ldt.\n");
		}
		if (error_code & (1 << 2)) {
			printk(COLOR_RED "    TI: selector in ldt.\n");
		} else {
			printk(COLOR_RED "    TI: selector in gdt.\n");
		}
		printk(
			COLOR_RED "    Selector: idx %d\n" COLOR_RESET,
			(int)((error_code & 0xfff8) >> 3));
	}

	io_hlt();
	while (1)
		;
}

void irq_return(void) {
	if (need_resched() && preempt_count() == 0) {
		get_current_thread()->flags.need_resched = 0;
		schedule();
	}
}

void do_irq(int irq) {
	disable_interrupt();
	hardirq_enter();
	device_irq_handler(irq);
	irq_table[irq](irq);

	interrupt_eoi(irq);
	hardirq_exit();
	enable_interrupt();

	do_softirq();

	irq_return();
}

void default_irq_handler(int irq) {
	// if (use_apic) {
	// 	apic_eoi();
	// } else {
	// 	pic_eoi(irq);
	// }
}
