#ifndef _DYN_ARRAY_H
#define _DYN_ARRAY_H

#include "stdint.h"
#include "types.h"

typedef struct DynArray {
	struct DynArrayBlock *first_block;
	struct DynArrayBlock *last_block;

	size_t size;
	size_t capacity;
	size_t block_size;
	size_t element_size;
} DynArray;

typedef struct DynArrayBlock {
	struct DynArrayBlock *next;
	void				 *data;
	int					  left_space;
} DynArrayBlock;

DynArray			 *dyn_array_new(size_t element_size, size_t block_size);
void				  dyn_array_delete(DynArray *dyn_array);
struct DynArrayBlock *dyn_array_find_block(DynArray *dyn_array, size_t idx);
void				 *dyn_array_new_item_addr(DynArray *dyn_array);
void				  dyn_array_remove(DynArray *dyn_array, void *item);

void *dyn_array_next_ptr(
	DynArray *dyn_array, struct DynArrayBlock **block, int *block_index,
	int *block_offset);
bool dyn_array_is_end(
	DynArray *dyn_array, struct DynArrayBlock *block, int block_index,
	int block_offset);

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

#define dyn_array_foreach(arr, type, var)                             \
	struct DynArrayBlock *_block		= (arr)->first_block;         \
	int					  _block_index	= 0;                          \
	int					  _block_offset = 0;                          \
	for (var = *((type *)_block->data);                               \
		 !dyn_array_is_end(arr, _block, _block_index, _block_offset); \
		 var = *((type *)dyn_array_next_ptr(                          \
			 arr, &_block, &_block_index, &_block_offset)))

#endif