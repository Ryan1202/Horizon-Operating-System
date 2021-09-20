#ifndef _DESC_H
#define _DESC_H

#include <kernel/thread.h>

#define DESC_G			0x8000
#define DESC_D			0x4000
#define DESC_L			0x2000
#define DESC_AVL		0x1000
#define DESC_P			0x80
#define DESC_DPL_3		0x60
#define DESC_DPL_2		0x40
#define DESC_DPL_1		0x20
#define DESC_DPL_0		0

#define DESC_S_CODE		0x10
#define DESC_S_DATA		0x10
#define DESC_S_SYS		0
#define DESC_TYPE_CODE	0x08
#define DESC_TYPE_DATA	0x02
#define DESC_TYPE_TSS	0x09

#define TI_GDT			0
#define TI_LDT			0x04
#define RPL0			0
#define RPL1			1
#define RPL2			2
#define RPL3			3

#define SELECTOR_K_CODE		(1 << 3) | TI_GDT | RPL0
#define SELECTOR_K_DATA		(2 << 3) | TI_GDT | RPL0
#define SELECTOR_K_STACK	SELECTOR_K_DATA
#define SElECTOR_TSS		(3 << 3) | TI_GDT | RPL0
#define SELECTOR_U_CODE		(4 << 3) | TI_GDT | RPL3
#define SELECTOR_U_DATA		(5 << 3) | TI_GDT | RPL3
#define SELECTOR_U_STACK	SELECTOR_U_DATA

struct segment_descriptor
{
    short limit_low, base_low;
    char base_mid, access_right;
    char limit_high, base_high;
};
struct gate_descriptor
{
    short offset_low, selector;
    char dw_count, access_right;
    short offset_high;
};

typedef void (*irq_handler_t) (int irq);

extern struct segment_descriptor	*gdt;
extern struct gate_descriptor		*idt;

extern void (*irq_enable)(int);

#define NR_IRQ          16

#define GDT_ADDR		0x200000
#define GDT_SIZE		0x7ff

#define IDT_ADDR		0x200800
#define IDT_SIZE		0x7ff

#define	DA_LDT			0x82	/* 局部描述符表段类型值		*/
#define	DA_TaskGate		0x85	/* 任务门类型值				*/
#define	DA_386TSS		0x89	/* 可用386任务状态段类型值	*/
#define	DA_386CGate		0x8C	/* 386调用门类型值			*/
#define	DA_386IGate		0x8E	/* 386中断门类型值			*/
#define	DA_386TGate		0x8F	/* 386陷阱门类型值			*/

void update_tss_esp(struct task_s *pthread);
void init_descriptor(void);
void put_irq_handler(int irq, irq_handler_t handler);
void set_segmdesc(struct segment_descriptor *sd, unsigned int limit, int base, int ar);
void set_gatedesc(struct gate_descriptor *gd, int offset, int selector, int ar);

#endif