/**
 * @file memory.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内存管理
 * @version 0.1
 * @date 2020-07
 */
#include <kernel/ards.h>
#include <kernel/console.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/memory/block.h>
#include <kernel/page.h>
#include <kernel/percpu.h>
#include <stdint.h>
#include <string.h>

struct ards *ards;

extern void *_kernel_start_phy;
extern void *_kernel_end_phy;
extern void *_kernel_end_vir;

extern void *VIR_BASE;
extern void *KERNEL_PHY_BASE;

extern size_t total_pages;
extern size_t allocated_pages;

void memory_early_init(void) {
	page_early_init((size_t)&_kernel_end_phy);
	setup_page();
}

void init_memory(void) {
	uint32_t ards_addr	  = ARDS_ADDR;
	uint32_t ards_nr_addr = ARDS_NR;
	uint16_t ards_nr	  = *((uint16_t *)ards_nr_addr); // ards 结构数
	ards				  = (struct ards *)ards_addr;	 // ards 地址

	page_init(ards, ards_nr, (size_t)&_kernel_start_phy);

	mem_caches_init();

	vmap_init();
}

int mmap_search(struct mmap *btmp, unsigned int cnt) {
	int index_byte = 0;
	int index_bit  = 0;
	while ((btmp->bits[index_byte] == 0xff) && (index_byte < btmp->len)) {
		index_byte++;
	}
	if (index_byte == btmp->len) { return -1; }

	while ((unsigned char)(1 << index_bit) & btmp->bits[index_byte]) {
		index_bit++;
	}
	int index_start = index_byte * 8 + index_bit;
	if (cnt == 1) { return index_start; }

	int bit_left = btmp->len * 8 - index_start - 1;
	int next_bit = index_start + 1;
	int count	 = 1;
	while (bit_left-- > 0) {
		if (!(btmp->bits[next_bit / 8] & 1 << (next_bit % 8))) {
			count++;
		} else {
			count = 0;
		}
		if (count == cnt) { return next_bit - cnt + 1; }
		next_bit++;
	}
	return index_start;
}

void mmap_set(struct mmap *btmp, unsigned int bit_index, int value) {
	unsigned int byte_idx = bit_index / 8; // 向下取整用于索引数组下标
	unsigned int bit_odd  = bit_index % 8; // 取余用于索引数组内的位

	/* 一般都会用个0x1这样的数对字节中的位操作,
	 * 将1任意移动后再取反,或者先取反再移位,可用来对位置0操作。*/
	if (value) { // 如果value为1
		btmp->bits[byte_idx] |= (1 << bit_odd);
	} else { // 若为0
		btmp->bits[byte_idx] &= ~(1 << bit_odd);
	}
}

int mmap_get(struct mmap *btmp, uint32_t bit_index) {
	uint32_t byte_idx = bit_index / 8;
	uint32_t bit_odd  = bit_index % 8;
	return (btmp->bits[byte_idx] & (1 << bit_odd)) != 0;
}

int get_memory_total_mib() {
	return total_pages * (PAGE_SIZE / 1024) / 1024;
}

int get_memory_usable_mib() {
	return (total_pages - allocated_pages) * (PAGE_SIZE / 1024) / 1024;
}

void print_memory_result(
	MemoryResult result, char *file, int line, char *func_with_args) {

	if (result == MEMORY_RESULT_OK) return;
	printk("[At file %s line%d: %s]", file, line, func_with_args);
	switch (result) {
		RESULT_CASE_PRINT(MEMORY_RESULT_OK)
		RESULT_CASE_PRINT(MEMORY_RESULT_INVALID_INPUT)
		RESULT_CASE_PRINT(MEMORY_RESULT_OUT_OF_MEMORY)
		RESULT_CASE_PRINT(MEMORY_RESULT_MEMORY_IS_USED)
	}
}
