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
#include <stdint.h>

typedef struct DmaOps {
	void *(*dma_alloc)(void *dma, uint32_t size);
	DriverResult (*dma_free)(void *dma, void *ptr, uint32_t size);
} DmaOps;

typedef struct Dma {
	void   *dma;
	DmaOps *ops;
	void   *param;
} Dma;

#endif