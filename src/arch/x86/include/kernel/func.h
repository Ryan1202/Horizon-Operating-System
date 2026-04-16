#ifndef _FUNC_H
#define _FUNC_H

#include <stdint.h>
#define CR0_PE 0x01
#define CR0_MP 0x02
#define CR0_EM 0x04
#define CR0_TS 0x08
#define CR0_ET 0x10
#define CR0_NE 0x20
#define CR0_WP 0x10000
#define CR0_AM 0x40000
#define CR0_NW 0x20000000
#define CR0_CD 0x40000000
#define CR0_PG 0x80000000

uint8_t	 io_in8(uint16_t port);
uint16_t io_in16(uint16_t port);
uint32_t io_in32(uint16_t port);

void io_out8(uint16_t port, uint8_t value);
void io_out16(uint16_t port, uint16_t value);
void io_out32(uint16_t port, uint32_t value);

void io_ins8(uint16_t port, void *dst, size_t count);
void io_ins16(uint16_t port, void *dst, size_t count);
void io_ins32(uint16_t port, void *dst, size_t count);

void io_outs8(uint16_t port, const void *src, size_t count);
void io_outs16(uint16_t port, const void *src, size_t count);
void io_outs32(uint16_t port, const void *src, size_t count);

void io_cli(void);
void io_sti(void);
void io_hlt(void);
void io_stihlt(void);

static inline void get_cpuid(
	uint32_t Mop, uint32_t Sop, uint32_t *a, uint32_t *b, uint32_t *c,
	uint32_t *d) {
	__asm__ __volatile__("cpuid"
						 : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
						 : "0"(Mop), "2"(Sop));
}

static inline void ltr(uint16_t sel) {
	__asm__ __volatile__("ltr %w0" ::"r"(sel));
}

static inline uint64_t bsr64(uint64_t x) {
	uint64_t index;
	__asm__ __volatile__("bsrq %1, %0" : "=r"(index) : "r"(x) : "cc");
	return index;
}

static inline uint64_t bsf64(uint64_t x) {
	uint64_t index;
	__asm__ __volatile__("bsfq %1, %0" : "=r"(index) : "r"(x) : "cc");
	return index;
}

static inline uint32_t bsr32(uint32_t x) {
	uint32_t index;
	__asm__ __volatile__("bsrl %1, %0" : "=r"(index) : "r"(x) : "cc");
	return index;
}

static inline uint32_t bsf32(uint32_t x) {
	uint32_t index;
	__asm__ __volatile__("bsfl %1, %0" : "=r"(index) : "r"(x) : "cc");
	return index;
}

static inline uint64_t read_tsc(void) {
	uint32_t low, high;
	__asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
	return ((uint64_t)high << 32) | low;
}

#define GET_REG(reg, var) __asm__ __volatile__("mov %%" reg ", %0" : "=g"(var));

size_t read_cr3();
void   write_cr3(size_t *cr3);
size_t read_cr2();
size_t read_cr0();
void   write_cr0(size_t cr0);

void enable_paging(void);

void load_gdtr(uint16_t limit, uint64_t addr);
void load_idtr(uint16_t limit, uint64_t addr);

int	 io_load_eflags(void);
void io_store_eflags(int eflags);

int save_eflags_cli(void);

void exception_entry0(void);
void exception_entry1(void);
void exception_entry2(void);
void exception_entry3(void);
void exception_entry4(void);
void exception_entry5(void);
void exception_entry6(void);
void exception_entry7(void);
void exception_entry8(void);
void exception_entry9(void);
void exception_entry10(void);
void exception_entry11(void);
void exception_entry12(void);
void exception_entry13(void);
void exception_entry14(void);
void exception_entry15(void);
void exception_entry16(void);
void exception_entry17(void);
void exception_entry18(void);
void exception_entry19(void);
void exception_entry20(void);
void exception_entry21(void);
void exception_entry22(void);
void exception_entry23(void);
void exception_entry24(void);
void exception_entry25(void);
void exception_entry26(void);
void exception_entry27(void);
void exception_entry28(void);
void exception_entry29(void);
void exception_entry30(void);
void exception_entry31(void);

void irq_entry0(void);
void irq_entry1(void);
void irq_entry2(void);
void irq_entry3(void);
void irq_entry4(void);
void irq_entry5(void);
void irq_entry6(void);
void irq_entry7(void);
void irq_entry8(void);
void irq_entry9(void);
void irq_entry10(void);
void irq_entry11(void);
void irq_entry12(void);
void irq_entry13(void);
void irq_entry14(void);
void irq_entry15(void);

void kernel_thread_entry(void);
void switch_to(size_t **cur, size_t **next);

#endif
