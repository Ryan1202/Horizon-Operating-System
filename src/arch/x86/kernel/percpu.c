#include <kernel/descriptor.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/percpu.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern char __percpu_start[];
extern char __percpu_end[];

void init_percpu(void) {
	void *page = kmalloc_pages(1);
	memcpy(page, __percpu_start, __percpu_end - __percpu_start);
	uint16_t selector = set_percpu_segment_descriptor(0, (size_t)page);
	asm volatile("mov %0, %%gs" : : "r"(selector));
}