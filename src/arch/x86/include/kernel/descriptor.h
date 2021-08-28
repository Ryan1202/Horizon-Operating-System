#ifndef _DESC_H
#define _DESC_H

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

void init_descriptor(void);
void put_irq_handler(int irq, irq_handler_t handler);
void set_segmdesc(struct segment_descriptor *sd, unsigned int limit, int base, int ar);
void set_gatedesc(struct gate_descriptor *gd, int offset, int selector, int ar);

#endif