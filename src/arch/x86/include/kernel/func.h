#ifndef _FUNC_H
#define _FUNC_H

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

int	 io_in8(unsigned int port);
int	 io_in16(unsigned int port);
int	 io_in32(unsigned int port);
void io_out8(unsigned int port, unsigned int data);
void io_out16(unsigned int port, unsigned int data);
void io_out32(unsigned int port, unsigned int data);
void io_read(unsigned short port, void *buf, unsigned int n);
void io_write(unsigned short port, void *buf, unsigned int n);
void io_cli(void);
void io_sti(void);
void io_hlt(void);
void io_stihlt(void);

static inline void get_cpuid(unsigned int Mop, unsigned int Sop, unsigned int *a, unsigned int *b,
							 unsigned int *c, unsigned int *d) {
	__asm__ __volatile__("cpuid	\n\t" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "0"(Mop), "2"(Sop));
}

static inline void ltr(unsigned short sel) {
	__asm__ __volatile__("ltr %0" ::"r"(sel));
}

static inline void port_insw(unsigned int port, unsigned int buffer, unsigned int nr) {
	__asm__ __volatile__("cld;\n\t \
        rep;\n\t \
        insw;\n\t \
        mfence;" ::"d"(port),
						 "D"(buffer), "c"(nr)
						 : "memory");
}

static inline void port_outsw(unsigned int port, unsigned int buffer, unsigned int nr) {
	__asm__ __volatile__("cld;\n\t \
        rep;\n\t \
        outsw;\n\t \
        mfence;\n\t" ::"d"(port),
						 "S"(buffer), "c"(nr)
						 : "memory");
}

static inline unsigned int bsr(unsigned int x) {
	unsigned int index;
	__asm__ __volatile__("bsrl %1, %0" : "=r"(index) : "r"(x));
	return index;
}

static inline unsigned int bsf(unsigned int x) {
	unsigned int index;
	__asm__ __volatile__("bsfl %1, %0" : "=r"(index) : "r"(x));
	return index;
}

#define GET_REG(reg, var) __asm__ __volatile__("mov %%" reg ", %0" : "=g"(var));

int	 read_cr3();
void write_cr3(unsigned int *cr3);
int	 read_cr2();
int	 read_cr0();
void write_cr0(int cr0);

void enable_paging(void);

void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);

int	 io_load_eflags(void);
void io_store_eflags(int eflags);

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

void switch_to(int *cur, int *next);

#endif