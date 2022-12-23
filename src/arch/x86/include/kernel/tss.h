#ifndef _TSS_C
#define _TSS_C

#include <stdint.h>

struct tss_s
{
	uint32_t back_link;
	uint32_t *esp0;
	uint32_t ss0;
	uint32_t *esp1;
	uint32_t ss1;
	uint32_t *esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t (*eip)(void);
	uint32_t eflags;
	uint32_t eax, ecx, edx, ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldt;
	uint32_t trace;
	uint32_t io_base;
};

void init_tss();

#endif