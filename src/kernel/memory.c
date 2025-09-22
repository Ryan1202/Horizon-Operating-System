/**
 * @file memory.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内存管理
 * @version 0.1
 * @date 2020-07
 */
#include "kernel/driver_interface.h"
#include "kernel/list.h"
#include <kernel/ards.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

uint32_t ards_addr	  = ARDS_ADDR;
uint32_t ards_nr_addr = ARDS_NR;

struct ards			 *ards;
struct mmap			  phy_page_mmap;
struct mmap			  vir_page_mmap;
struct memory_manage *memory_manage;

uint32_t memory_total_size;

const int memory_block_size[MEMORY_FREE_LIST_COUNT] = {32,	64,	  128, 256,
													   512, 1024, 2048};

void init_memory(void) {
	uint16_t ards_nr = *((uint16_t *)ards_nr_addr); // ards 结构数
	ards			 = (struct ards *)ards_addr;	// ards 地址
	int i;
	for (i = 0; i < ards_nr; i++) {
		// 寻找可用最大内存
		if (ards->type == 1) {
			if (ards->base_low + ards->length_low > memory_total_size) {
				memory_total_size = ards->base_low + ards->length_low;
			}
		}
		ards++;
	}

	int page_bytes =
		(memory_total_size - PHY_MEM_BASE_ADDR - KERN_VIR_MEM_BASE_ADDR) /
		(PAGE_SIZE * 8);

	phy_page_mmap.bits = (unsigned char *)PHY_MEM_MMAP;
	phy_page_mmap.len  = page_bytes;

	vir_page_mmap.bits = (unsigned char *)VIR_MEM_MMAP;
	vir_page_mmap.len =
		PHY_MEM_MMAP_SIZE -
		(PHY_MEM_BASE_ADDR + KERN_VIR_MEM_BASE_ADDR) / (PAGE_SIZE * 8);

	memset(phy_page_mmap.bits, 0, phy_page_mmap.len);
	memset(vir_page_mmap.bits, 0, vir_page_mmap.len);

	unsigned int memory_manage_pages =
		DIV_ROUND_UP(sizeof(struct memory_manage), PAGE_SIZE);
	memory_manage =
		(struct memory_manage *)kernel_alloc_pages(memory_manage_pages);
	memset(memory_manage, 0, memory_manage_pages * PAGE_SIZE);
	memory_manage->last_free_block = 0;
	for (i = 0; i < MEMORY_FREE_LIST_COUNT; i++) {
		list_init(&memory_manage->free_blocks_list[i]);
	}
	for (i = 0; i < MEMORY_BLOCKS; i++) {
		memory_manage->free_blocks[i].size	= 0; // 大小是页的数量
		memory_manage->free_blocks[i].flags = 0;
	}
}

int get_memory_size(void) {
	return memory_total_size / 1024 / 1024;
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

MemoryResult alloc_vaddr(size_t in_size, uint32_t *out_vaddr) {
	in_size = ((in_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
	if (!in_size) return MEMORY_RESULT_INVALID_INPUT;

	uint32_t pages = in_size / PAGE_SIZE;

	/* 扫描获取请求的页数 */
	int idx = mmap_search(&vir_page_mmap, pages);
	if (idx == -1) return MEMORY_RESULT_OUT_OF_MEMORY;

	int i;
	/* 把已经扫描到的位设置为1，表明已经分配了 */
	for (i = 0; i < pages; i++) {
		mmap_set(&vir_page_mmap, idx + i, 1);
	}

	/* 返还转换好的虚拟地址 */
	*out_vaddr = KERN_VIR_MEM_BASE_ADDR + VIR_MEM_BASE_ADDR + idx * PAGE_SIZE;
	return MEMORY_RESULT_OK;
}

int find_continuous_free_block(struct memory_manage *mm, int count) {
	int k;
	for (k = mm->last_free_block; k < MEMORY_BLOCKS; k++) {
		if (mm->free_blocks[k].flags == MEMORY_BLOCK_FREE) {
			for (int i = 0; i < count; i++) {
				if (mm->free_blocks[k + i].flags != MEMORY_BLOCK_FREE) {
					break;
				}
			}
		}
	}
	if (k == MEMORY_BLOCKS) {
		for (k = 0; k < mm->last_free_block; k++) {
			for (int i = 0; i < count; i++) {
				if (mm->free_blocks[k + i].flags != MEMORY_BLOCK_FREE) {
					break;
				}
			}
		}
		if (k == mm->last_free_block) return -1;
	}
	mm->last_free_block = k + count;
	return k;
}

int find_free_block(struct memory_manage *mm) {
	int i;
	for (i = mm->last_free_block; i < MEMORY_BLOCKS; i++) {
		if (mm->free_blocks[i].flags == MEMORY_BLOCK_FREE) {
			mm->last_free_block = i;
			return i;
		}
	}
	for (i = 0; i < mm->last_free_block; i++) {
		if (mm->free_blocks[i].flags == MEMORY_BLOCK_FREE) {
			mm->last_free_block = i;
			return i;
		}
	}
	return -1;
}

bool split_page(struct memory_manage *mm, size_t page_addr, int pow) {
	uint32_t size	   = 1 << pow;
	int		 break_cnt = PAGE_SIZE >> pow; // 打散成break_cnt个

	int index = find_free_block(mm);
	if (index == -1) return false;
	index--; // 因为下面会自增，所以这里先-1

	uint32_t addr = page_addr;
	for (int i = 0; i < break_cnt; i++) {
		index++;
		if (mm->free_blocks[index].size || index >= MEMORY_BLOCKS) {
			index = find_free_block(mm);
			if (index == -1) return false;
		}
		mm->free_blocks[index].address = addr;
		mm->free_blocks[index].size	   = size;
		mm->free_blocks[index].flags   = MEMORY_BLOCK_USING;
		mm->free_blocks[index].mode	   = MEMORY_BLOCK_MODE_SMALL;
		list_add_tail(
			&mm->free_blocks[index].list,
			&mm->free_blocks_list[pow - MEMORY_MIN_POW]);
		addr += size;
	}
	return true;
}

// 默认对齐32字节
void *kmalloc(uint32_t size) {
	void *address;
	void *new_address;

	if (size == 0) { return NULL; }

	int flags = save_and_disable_interrupt(); // TODO
	// 大于半个页就按页分配
	if (size > 2048) {
		int pages = (size + PAGE_SIZE - 1) >> 12; // 一共占多少个页
		int index = find_free_block(memory_manage);
		if (index == -1) {
			store_interrupt_status(flags);
			return NULL;
		}
		address = kernel_alloc_pages(pages); // 分配页
		if (address == NULL) {
			store_interrupt_status(flags);
			return NULL;
		}
		memory_manage->free_blocks[index].address = (uint32_t)address;
		memory_manage->free_blocks[index].size	  = pages; // 大小是页的数量
		memory_manage->free_blocks[index].flags	  = MEMORY_BLOCK_ALLOCATED;
		memory_manage->free_blocks[index].mode	  = MEMORY_BLOCK_MODE_BIG;
		store_interrupt_status(flags);
		return (void *)address;
	} else if (0 < size && size <= 2048) {					   // size <= 2048
		int pow = MAX(aligned_up_log2n(size), MEMORY_MIN_POW); // 指数
		size	= 1 << pow;
		// 第一次寻找，如果在块中没有找到，就打散一个页
		if (!list_empty(
				&memory_manage->free_blocks_list[pow - MEMORY_MIN_POW])) {
			struct memory_block *block = list_first_owner(
				&memory_manage->free_blocks_list[pow - MEMORY_MIN_POW],
				struct memory_block, list);
			address		 = (void *)block->address;
			block->flags = MEMORY_BLOCK_ALLOCATED;
			list_del(&block->list);
			store_interrupt_status(flags);
			return (void *)address;
		}
		// 如果都没有找到，分配一个页，然后打散
		// 分配一个页，用来被打散
		new_address = kernel_alloc_pages(1);
		if (new_address == NULL) {
			store_interrupt_status(flags);
			return NULL;
		}
		if (!split_page(memory_manage, (size_t)new_address, pow)) {
			store_interrupt_status(flags);
			return NULL;
		}

		// 打散后再寻找
		struct memory_block *block = list_first_owner(
			&memory_manage->free_blocks_list[pow - MEMORY_MIN_POW],
			struct memory_block, list);
		address		 = (void *)block->address;
		block->flags = MEMORY_BLOCK_ALLOCATED;
		list_del(&block->list);
		store_interrupt_status(flags);
		return (void *)address;
	}
	// size=0或者没有找到
	store_interrupt_status(flags);
	return NULL; // 失败
}

int kfree(void *address) {
	if (address == NULL) { return 0; }
	int					 i;
	uint32_t			 addr = (uint32_t)address;
	struct memory_block *block;

	int flags = save_and_disable_interrupt();
	for (i = 0; i < MEMORY_BLOCKS; i++) {
		block = &memory_manage->free_blocks[i];
		if (block->address == addr && block->flags == MEMORY_BLOCK_ALLOCATED) {
			if (block->mode == MEMORY_BLOCK_MODE_BIG) {
				kernel_free_page(block->address, block->size);
				block->flags = MEMORY_BLOCK_FREE;
				block->size	 = 0; // 只有大块才需要重新设置size
				store_interrupt_status(flags);
				return 0;
			} else if (block->mode == MEMORY_BLOCK_MODE_SMALL) {
				int pow		 = aligned_up_log2n(block->size);
				block->flags = MEMORY_BLOCK_USING;
				list_add_tail(
					&block->list,
					&memory_manage->free_blocks_list[pow - MEMORY_MIN_POW]);
				// 存在一种情况，那就是所有被打散的内存都被释放后，可能需要释放那个页，目前还没有考虑它
				// 小块不需要设置大小，因为就是打散了的块
				store_interrupt_status(flags);
				return 0;
			}
		}
	}

	return -1; // 失败
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
