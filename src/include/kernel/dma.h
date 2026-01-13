/**
 * @file dma.h
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief 统一的DMA接口
 * @version 0.1
 * @date 2025-03-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef _DMA_H
#define _DMA_H

#include "kernel/driver.h"
#include "kernel/list.h"
#include <stdint.h>

struct Dma;
typedef struct DmaOps {
	void *(*dma_alloc)(struct Dma *dma, uint32_t size);
	DriverResult (*dma_free)(struct Dma *dma, void *ptr, uint32_t size);

	DriverResult (*dma_map_buffer)(struct Dma *dma, void *ptr, uint32_t size);
	DriverResult (*dma_unmap_buffer)(struct Dma *dma, void *ptr, uint32_t size);
} DmaOps;

typedef struct Dma {
	void   *dma;
	DmaOps *ops;
	void   *param;
} Dma;

typedef struct DmaSegment {
	list_t list;
	size_t vaddr;
	size_t addr;
	size_t size;
} DmaSegment;

DriverResult dma_split_mem(
	list_t *lh, void *ptr, uint32_t size, int max_segment_size);

#endif