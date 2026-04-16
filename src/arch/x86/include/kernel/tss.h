#ifndef _TSS_C
#define _TSS_C

#include <stdint.h>

struct tss_s {
	uint32_t reserved0;
	uint64_t rsp[3];
	uint64_t reserved1;
	uint64_t ist[7];
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t io_base;
} __attribute__((packed));

void init_tss();

#endif