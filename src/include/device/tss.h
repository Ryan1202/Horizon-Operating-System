#ifndef _TSS_C
#define _TSS_C

#include <stdint.h>

struct tss_s
{
	uint16_t back_link, _blh;
	uint32_t esp0;
	uint16_t ss0, _ss0h;
	uint32_t esp1;
	uint16_t ss1, _ss1h;
	uint32_t esp2;
	uint16_t ss2, _ss2h;
	uint32_t _cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax, ecx, edx, ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint16_t es, _esh;
	uint16_t cs, _csh;
	uint16_t ss, _ssh;
	uint16_t ds, _dsh;
	uint16_t fs, _fsh;
	uint16_t gs, _gsh;
	uint16_t ldt, _ldth;
	uint16_t trace, bitmap;
	// uint32_t io_bitmap[32 + 1];
	
	// uint32_t _cacheline_fiiler[5];
};

void init_tss();

#endif