#include <dyn_array.h>
#include <kernel/memory.h>
#include <math.h>
#include <stdint.h>
#include <types.h>

DynArray *dyn_array_new(size_t element_size, size_t block_size) {
	DynArray *array = kmalloc(sizeof(DynArray));
	if (array == NULL) { return NULL; }

	array->size			= 0;
	array->capacity		= block_size;
	array->block_size	= block_size;
	array->element_size = find_next_pow_of_2(element_size);

	array->first_block		 = kmalloc(sizeof(struct DynArrayBlock));
	array->first_block->next = NULL;
	array->first_block->data = kmalloc(block_size * array->element_size);
	array->last_block		 = array->first_block;

	return array;
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
	if (dyn_array->size % dyn_array->block_size == 0 && dyn_array->size != 0) {
		dyn_array_extend_block(dyn_array);
		block = dyn_array->last_block;
	}

	return block->data +
		   (dyn_array->size % dyn_array->block_size) * dyn_array->element_size;
}