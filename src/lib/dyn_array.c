#include <dyn_array.h>
#include <kernel/memory.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

DynArray *dyn_array_new(size_t element_size, size_t block_size) {
	DynArray *array = kmalloc(sizeof(DynArray));
	if (array == NULL) { return NULL; }

	array->size			= 0;
	array->capacity		= block_size;
	array->block_size	= block_size;
	array->element_size = find_next_pow_of_2(element_size);

	array->first_block			   = kmalloc(sizeof(struct DynArrayBlock));
	array->first_block->next	   = NULL;
	array->first_block->data	   = kmalloc(block_size * array->element_size);
	array->first_block->left_space = array->block_size;
	array->last_block			   = array->first_block;

	return array;
}

void dyn_array_delete(DynArray *dyn_array) {
	struct DynArrayBlock *block = dyn_array->first_block;
	while (block != NULL) {
		struct DynArrayBlock *next = block->next;
		kfree(block->data);
		kfree(block);
		block = next;
	}
	kfree(dyn_array);
}

void dyn_array_extend_block(DynArray *dyn_array) {
	struct DynArrayBlock *block = dyn_array->first_block;
	while (block->next != NULL) {
		block = block->next;
	}

	struct DynArrayBlock *new_block = kmalloc(sizeof(struct DynArrayBlock));

	block->next		= new_block;
	new_block->next = NULL;
	new_block->data = kmalloc(dyn_array->block_size * dyn_array->element_size);
	new_block->left_space = dyn_array->block_size;

	dyn_array->capacity += dyn_array->block_size * dyn_array->element_size;

	dyn_array->last_block = new_block;
}

struct DynArrayBlock *dyn_array_find_block(DynArray *dyn_array, size_t idx) {
	struct DynArrayBlock *block = dyn_array->first_block;

	for (int i = 0; i < idx; i++) {
		if (block->next == NULL) { dyn_array_extend_block(dyn_array); }
		block = block->next;
	}

	return block;
}

/**
 * @brief 找到新的元素的地址
 *
 */
void *dyn_array_new_item_addr(DynArray *dyn_array) {
	struct DynArrayBlock *block = dyn_array->last_block;
	if (block->left_space == 0 && dyn_array->size != 0) {
		dyn_array_extend_block(dyn_array);
		block = dyn_array->last_block;
	}
	block->left_space--;

	return block->data +
		   (dyn_array->size % dyn_array->block_size) * dyn_array->element_size;
}

// 以下两个函数都是为foreach服务的

void *dyn_array_next_ptr(
	DynArray *dyn_array, struct DynArrayBlock **block, int *block_index,
	int *block_offset) {

	struct DynArrayBlock *current_block = *block;
	int					  offset		= *block_offset;

	if (offset + 1 < dyn_array->block_size) {
		(*block_offset)++;
	} else {
		(*block_index)++;
		*block_offset = 0;
		*block		  = (*block)->next;
	}
	return (void *)(*block)->data + (*block_offset) * dyn_array->element_size;
}

void dyn_array_remove(DynArray *dyn_array, void *item) {
	struct DynArrayBlock *block		 = dyn_array->first_block;
	struct DynArrayBlock *last_block = NULL;

	while (block != NULL) {
		for (int i = 0; i < dyn_array->block_size; i++) {
			if (block->data + i * dyn_array->element_size == item) {
				memset(
					block->data + i * dyn_array->element_size, 0,
					dyn_array->element_size);

				dyn_array->size--;
				block->left_space--;
				if (block->left_space == 0 && block != dyn_array->first_block) {
					if (last_block != NULL) {
						last_block->next = block->next;
					} else {
						dyn_array->first_block = block->next;
					}
					if (dyn_array->last_block == block) {
						dyn_array->last_block = last_block;
					}
					kfree(block);
				}
				return;
			}
		}
		last_block = block;
		block	   = block->next;
	}
}

bool dyn_array_is_end(
	DynArray *dyn_array, struct DynArrayBlock *block, int block_index,
	int block_offset) {
	return block == NULL &&
		   block_offset == dyn_array->size % dyn_array->block_size;
}