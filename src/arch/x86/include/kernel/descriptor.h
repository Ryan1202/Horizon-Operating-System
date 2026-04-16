#ifndef _DESC_H
#define _DESC_H

#include <bits.h>
#include <kernel/thread.h>
#include <stdint.h>

#define DESC_G		BIT(15)
#define DESC_D		BIT(14)
#define DESC_L		BIT(13)
#define DESC_AVL	BIT(12)
#define DESC_P		BIT(7)
#define DESC_DPL(n) ((n) << 5)
#define DESC_S		BIT(4)

// Segment Descriptor Type
#define DESC_TYPE_CODE 10
#define DESC_TYPE_DATA 2

// System Descriptor Type
#define DESC_TYPE_TSS 9
#define DESC_TYPE_LDT 2
#define DESC_TYPE_INT 14

#define TI_GDT 0
#define TI_LDT BIT(2)
#define RPL(n) (n)

#define SELECTOR_K_CODE	 (1 << 3) | TI_GDT | RPL(0)
#define SELECTOR_K_DATA	 (2 << 3) | TI_GDT | RPL(0)
#define SELECTOR_K_STACK SELECTOR_K_DATA
#define SELECTOR_U_CODE	 (3 << 3) | TI_GDT | RPL(3)
#define SELECTOR_U_DATA	 (4 << 3) | TI_GDT | RPL(3)
#define SELECTOR_U_STACK SELECTOR_U_DATA
#define SELECTOR_TSS	 (5 << 3) | TI_GDT | RPL(0)

struct segment_descriptor {
	uint16_t limit_low, base_low;
	uint8_t	 base_mid, access_right;
	uint8_t	 limit_high, base_high;
} __attribute__((packed));
struct gate_descriptor {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t	 ist;
	uint8_t	 access_right;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t reserved;
} __attribute__((packed));

typedef void (*irq_handler_t)(int irq);

extern struct segment_descriptor *gdt;
extern struct gate_descriptor	 *idt;

#define NR_IRQ 16

#define GDT_BASE 0xffffffff80000800
#define GDT_SIZE 0x7ff

#define IDT_BASE 0xffffffff80001000
#define IDT_SIZE 0xfff

#define DA_LDT			 0x82 /* 局部描述符表段类型值		*/
#define DA_TaskGate		 0x85 /* 任务门类型值				*/
#define DA_386TSS		 0x89 /* 可用386任务状态段类型值	*/
#define DA_386CGate		 0x8C /* 386调用门类型值			*/
#define DA_386IGate_DPL0 0x8E /* 386中断门类型值(0特权级)	*/
#define DA_386IGate_DPL3 0xEE /* 386中断门类型值(3特权级)	*/
#define DA_386TGate		 0x8F /* 386陷阱门类型值			*/

void update_tss_esp(struct task_s *pthread);
void init_descriptor(void);
void put_irq_handler(int irq, irq_handler_t handler);
void set_segment_descriptor(
	struct segment_descriptor *sd, unsigned int limit, uint64_t base, int ar);
void set_gate_descriptor(
	struct gate_descriptor *gd, uint64_t offset, uint16_t selector, uint8_t ar);
uint16_t set_percpu_segment_descriptor(int cpu_id, size_t addr);

#endif