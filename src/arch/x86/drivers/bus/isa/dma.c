/**
 * @file dma.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief (ISA)DMA驱动
 * @version 0.1
 * @date 2021-7
 */
#include "kernel/driver.h"
#include "math.h"
#include <drivers/bus/isa/dma.h>
#include <kernel/device.h>
#include <kernel/dma.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

DmaOps isa_dma_ops = {
	.dma_alloc = dma_alloc_region,
	.dma_free  = dma_free_region,
};

#define DMA_MEM_BASE_ADDR 0x800000

Device *dma_channels[8]; // 使用DMA通道的设备

struct mmap dma_mem_mmap;
spinlock_t	dma_spin_lock;

int dma_lock() {
	return spin_lock_irqsave(&dma_spin_lock);
}

void dma_unlock(int flags) {
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}

void dma_init() {
	spinlock_init(&dma_spin_lock);
	dma_mem_mmap.bits = kmalloc(DMA_MAX_REGION_COUNT / 8);
	dma_mem_mmap.len  = DMA_MAX_REGION_COUNT / 8;
	memset(dma_mem_mmap.bits, 0, dma_mem_mmap.len);
}

void *dma_alloc_region(Dma *dma, uint32_t size) {
	int cnt = DIV_ROUND_UP(size, 64 * 1024);
	int idx = mmap_search(&dma_mem_mmap, cnt);
	if (idx == -1) return NULL;

	for (int i = 0; i < cnt; i++) {
		mmap_set(&dma_mem_mmap, idx + i, 1);
	}
	return (void *)(DMA_MEM_BASE_ADDR + idx * DMA_REGION_SIZE);
}

DriverResult dma_free_region(Dma *dma, void *ptr, uint32_t size) {
	int cnt = DIV_ROUND_UP(size, 64 * 1024);
	int idx = ((uint32_t)ptr - DMA_MEM_BASE_ADDR) / DMA_REGION_SIZE;
	for (int i = 0; i < cnt; i++) {
		mmap_set(&dma_mem_mmap, idx + i, 0);
	}
	return DRIVER_RESULT_OK;
}

uint32_t get_dma_count(int channel) {
	uint16_t count;
	uint8_t	 port;
	if (channel < 4) {
		port = DMA0 + (channel << 1) + 1;
	} else {
		port = DMA1 + ((channel & 3) << 2) + 2;
	}
	count = io_in8(port);
	count += io_in8(port) << 8;
	count++;
	return channel < 4 ? count : count << 1;
}

uint32_t dma_pointer(int channel, uint32_t size) {
	uint32_t result;

	int flags = dma_lock();
	dma_ff_reset(channel);
	result = get_dma_count(channel);
	dma_unlock(flags);

	if (result >= size || result == 0) return 0;
	else return size - result;
}

int dma_channel_use(Device *device, int *possible_ch, int len) {
	for (int i = 0; i < len; i++) {
		if (dma_channels[possible_ch[i]] == NULL) {
			dma_channels[possible_ch[i]] = device;
			return possible_ch[i];
		}
	}
	return -1;
}

void dma_channel_unuse(Device *device, uint8_t channel) {
	if (channel < 8) {
		if (dma_channels[channel] == device) dma_channels[channel] = NULL;
	}
}

void dma_enable(unsigned int channel) {
	if (channel < 4) {
		io_out8(DMA1_REG_MASK, channel);
	} else {
		io_out8(DMA2_REG_MASK, channel & 3);
	}
}

void dma_disable(unsigned int channel) {
	if (channel < 4) {
		io_out8(DMA1_REG_MASK, channel | 4);
	} else {
		io_out8(DMA2_REG_MASK, (channel & 3) | 4);
	}
}

void dma_ff_reset(unsigned int channel) {
	if (channel < 4) {
		io_out8(DMA1_REG_FF_RESET, 0);
	} else {
		io_out8(DMA2_REG_FF_RESET, 0);
	}
}

void dma_set_mode(unsigned int channel, char mode) {
	if (channel < 4) {
		io_out8(DMA1_REG_MODE, mode | channel);
	} else {
		io_out8(DMA2_REG_MODE, mode | (channel & 3));
	}
}

void dma_set_page(unsigned int channel, char page) {
	switch (channel) {
	case 0:
		io_out8(DMA_PAGE0, page);
		break;
	case 1:
		io_out8(DMA_PAGE1, page);
		break;
	case 2:
		io_out8(DMA_PAGE2, page);
		break;
	case 3:
		io_out8(DMA_PAGE3, page);
		break;
	case 5:
		io_out8(DMA_PAGE5, page & 0xfe);
		break;
	case 6:
		io_out8(DMA_PAGE6, page & 0xfe);
		break;
	case 7:
		io_out8(DMA_PAGE7, page & 0xfe);
		break;

	default:
		break;
	}
}

void dma_set_addr(unsigned int channel, unsigned int addr) {
	dma_set_page(channel, addr >> 16);
	if (channel <= 3) {
		io_out8(DMA0 + ((channel & 3) << 1), addr & 0xff);
		io_out8(DMA0 + ((channel & 3) << 1), (addr >> 8) & 0xff);
	} else {
		io_out8(DMA1 + ((channel & 3) << 2), (addr >> 1) & 0xff);
		io_out8(DMA1 + ((channel & 3) << 2), (addr >> 9) & 0xff);
	}
}

void dma_set_count(unsigned int channel, unsigned int count) {
	count--;
	if (channel <= 3) {
		io_out8(DMA0 + ((channel & 3) << 1) + 1, count & 0xff);
		io_out8(DMA0 + ((channel & 3) << 1) + 1, (count >> 8) & 0xff);
	} else {
		io_out8(DMA1 + ((channel & 3) << 2) + 2, (count >> 1) & 0xff);
		io_out8(DMA1 + ((channel & 3) << 2) + 2, (count >> 9) & 0xff);
	}
}
