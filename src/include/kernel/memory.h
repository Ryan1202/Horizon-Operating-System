#ifndef _MEMORY_H
#define _MEMORY_H

#include "kernel/list.h"
#include "result.h"
#include <stdint.h>

#define MEMORY_BLOCKS		   0x1000
#define MEMORY_FREE_LIST_COUNT 7

#define MEMORY_MIN_POW 5

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
	list_t		 list;
	unsigned int address;
	int			 size;
	int			 flags;
	int			 mode;
};

struct mem_cache;

void memory_early_init(void);
void init_memory(void);
int	 get_memory_usable_mib(void);
int	 get_memory_total_mib(void);

int	 mmap_search(struct mmap *btmp, unsigned int cnt);
void mmap_set(struct mmap *btmp, unsigned int bit_index, int value);
int	 mmap_get(struct mmap *btmp, uint32_t bit_index);

void *kmalloc(size_t size);
void *kzalloc(size_t size);
int	  kfree(void *address);

struct mem_cache *mem_cache_create(
	const char *name, size_t object_size, size_t align);
int	  mem_cache_destroy(struct mem_cache *cache);
void *mem_cache_alloc(struct mem_cache *cache);

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

// Rust bindings
typedef enum ZoneType {
	ZONE_LINEAR = 0,
	ZONE_MEM32	= 1,
} ZoneType;

void mem_caches_init();
void vmap_init();

#endif