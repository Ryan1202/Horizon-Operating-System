#ifndef __KERNEL_MEMORY_BLOCK_H
#define __KERNEL_MEMORY_BLOCK_H

#include <kernel/ards.h>
#include <stdint.h>

void   page_early_init(size_t kernel_end);
size_t early_allocate_pages(uint8_t count);
void   page_init(struct ards *ards, uint16_t ards_nr, size_t kernel_start);

#endif