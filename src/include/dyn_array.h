#ifndef _DYN_ARRAY_H
#define _DYN_ARRAY_H

#include "stdint.h"

typedef struct DynArray {
	struct DynArrayBlock *first_block;
	struct DynArrayBlock *last_block;

	size_t size;
	size_t capacity;
	size_t block_size;
	size_t element_size;
} DynArray;

struct DynArrayBlock {
	struct DynArrayBlock *next;
	void				 *data;
};

DynArray			 *dyn_array_new(size_t element_size, size_t block_size);
struct DynArrayBlock *dyn_array_find_block(DynArray *dyn_array, size_t idx);
void				 *dyn_array_new_item_addr(DynArray *dyn_array);

#define dyn_array_get(arr, type, idx)                                         \
	(type)({                                                                  \
		int block_index	 = (idx) / (arr)->block_size;                         \
		int block_offset = (idx) % (arr)->block_size;                         \
                                                                              \
		struct DynArrayBlock *block = dyn_array_find_block(arr, block_index); \
		((type *)block->data)[block_offset];                                  \
	})

#define dyn_array_set(arr, type, idx, value)                                  \
	{                                                                         \
		int block_index	 = (idx) / (arr)->block_size;                         \
		int block_offset = (idx) % (arr)->block_size;                         \
                                                                              \
		struct DynArrayBlock *block = dyn_array_find_block(arr, block_index); \
		((type *)block->data)[block_offset] = value;                          \
	}

#define dyn_array_append(arr, type, value)         \
	{                                              \
		type *addr = dyn_array_new_item_addr(arr); \
		*addr	   = value;                        \
		(arr)->size++;                             \
	}

#endif