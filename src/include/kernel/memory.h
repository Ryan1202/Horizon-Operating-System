#ifndef _MEMORY_H
#define _MEMORY_H

#include "result.h"
#include <stddef.h>
#include <stdint.h>

#define PHY_MEM_BASE_ADDR 0x1000000
#define PHY_MEM_MMAP	  0x210000
#define PHY_MEM_MMAP_SIZE 0x20000
#define VIR_MEM_BASE_ADDR 0x1000000
#define VIR_MEM_MMAP	  0x230000
#define VIR_MEM_MMAP_SIZE 0x10000

#define KERN_VIR_MEM_BASE_ADDR 0x00000000
#define USER_VIR_MEM_BASE_ADDR 0x80000000

#define MEMORY_BLOCKS			0x1000
#define MEMORY_BLOCK_FREE		0 // 内存信息块空闲
#define MEMORY_BLOCK_USING		1 // 内存信息块使用中
#define MEMORY_BLOCK_MODE_SMALL 0 // 小块内存描述1024一下的内存块
#define MEMORY_BLOCK_MODE_BIG	1 // 大块内存描述4kb为单位的内存块

extern struct mmap phy_page_mmap;
extern struct mmap vir_page_mmap;

typedef enum MemoryResult {
	MEMORY_RESULT_OK,
	MEMORY_RESULT_INVALID_INPUT,
	MEMORY_RESULT_OUT_OF_MEMORY,
	MEMORY_RESULT_MEMORY_IS_USED,
} MemoryResult;

struct mmap {
	int			   len;
	unsigned char *bits;
};

struct memory_block {
	unsigned int address;
	int			 size;
	int			 flags;
	int			 mode;
};

struct memory_manage {
	struct memory_block free_blocks[MEMORY_BLOCKS];
};

void		 init_memory(void);
int			 get_memory_size(void);
int			 mmap_search(struct mmap *btmp, unsigned int cnt);
void		 mmap_set(struct mmap *btmp, unsigned int bit_index, int value);
int			 mmap_get(struct mmap *btmp, uint32_t bit_index);
MemoryResult alloc_vaddr(size_t in_size, uint32_t *out_vaddr);
void		*kmalloc(uint32_t size);
int			 kfree(void *address);

void print_memory_result(
	MemoryResult result, char *file, int line, char *func_with_args);

#define MEM_PRINT_RESULT(result, func, ...) \
	print_memory_result(result, __FILE__, __LINE__, #func "(" #__VA_ARGS__ ")");
#define MEMORY_RESULT_DELIVER_CALL(func, ...) \
	RESULT_DELIVER_CALL(                      \
		MemoryResult, MEMORY_RESULT_OK, func, \
		{ MEM_PRINT_RESULT(result, func, __VA_ARGS__); }, __VA_ARGS__)
#define MEMORY_RESULT_PRINT_CALL(func, ...)              \
	({                                                   \
		MemoryResult result = func(__VA_ARGS__);         \
		if (result != MEMORY_RESULT_OK) {                \
			MEM_PRINT_RESULT(result, func, __VA_ARGS__); \
		}                                                \
		result;                                          \
	})

#endif