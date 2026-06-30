#include "include/dma.h"
#include "include/ide.h"
#include "include/ide_controller.h"
#include "kernel/driver.h"
#include "kernel/driver_interface.h"
#include "kernel/list.h"
#include "stdint.h"
#include <kernel/dma.h>
#include <kernel/page.h>
#include <math.h>
#include <string.h>

/**
 * 配置DMA
 */
void ata_bmdma_set_prdt(IdeDevice *device, AtaDma *ata_dma, int rw) {
	IdeChannel *channel = device->channel;

	PhysicalRegionDescriptor *prds = ata_dma->prds;

	int		 i = 0, size, left_size;
	uint32_t addr, offset;

	for (int j = 0; j < ata_dma->sg_nents; j++) {
		DmaScatterList *seg = &ata_dma->sg_list[j];
		left_size = sg_dma_len(seg);
		addr	  = sg_dma_address(seg);
		while (left_size > 0 && i < IDE_MAX_PRDT_COUNT) {
			offset			  = addr & 0xffff; // 缓冲区不能跨越64K边界
			size			  = MIN(left_size, 0x10000 - offset);
			prds[i].base_addr = addr;
			prds[i].count	  = size & 0xffff;
			prds[i].sign	  = 0;
			left_size -= size;
			addr += size;
			i++;
		}
	}
	if (i == 0) { return; }
	prds[i - 1].sign = BIT(15);

	io_out_dword(channel->bmide + IDE_REG_BM_PRDT, ata_dma->prdt_dma_addr);

	uint8_t data;
	data = io_in_byte(channel->bmide + IDE_REG_BM_COMMAND);
	data = (rw == 0) ? BIN_EN(data, IDE_BMCMD_READ_WRITE)
					 : BIN_DIS(data, IDE_BMCMD_READ_WRITE);
	io_out_byte(channel->bmide + IDE_REG_BM_COMMAND, data);

	// 清除 BM 状态 (W1C:
	// 只写入要清除的位，不能读-改-写，否则会把ACTIVE等只读位写入)
	io_out_byte(
		channel->bmide + IDE_REG_BM_STATUS,
		IDE_BMSTATUS_INT | IDE_BMSTATUS_ERROR);
}

inline DriverResult ata_bmdma_map_sg(
	AtaDma *ata_dma, void *ptr, uint32_t size, int rw) {
	DmaDirection direction = rw ? DmaToDevice : DmaFromDevice;

	if (ata_dma->sg_list != NULL) {
		// 意料之外的情况，无法确定指针是否有效，兜底方案直接泄露
		print_warning(
			"ATA BMDMA",
			"Previous DMA mapping pointer (%#x) still exists, leaking...\n",
			(size_t)ata_dma->sg_list);
	}

	// 计算需要的entry数量（每个页面一个entry）
	uint32_t nents = ((size_t)ptr + size + PAGE_SIZE - 1) / PAGE_SIZE
		- (size_t)ptr / PAGE_SIZE;
	if (nents == 0) nents = 1;

	DmaScatterList *sg = sg_create_segment(nents);
	if (sg == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;

	// 简化处理：假设ptr指向的缓冲区是连续的虚拟地址
	// 将其拆分为页面级别的scatter entry
	void *cur = ptr;
	uint32_t remaining = size;
	for (uint32_t i = 0; i < nents && remaining > 0; i++) {
		uint32_t page_offset = (size_t)cur & (PAGE_SIZE - 1);
		uint32_t len = MIN(remaining, PAGE_SIZE - page_offset);
		sg_set_buf(&sg[i], cur, len);
		cur += len;
		remaining -= len;
	}

	int mapped = dma_map_sg(ata_dma->dma_device, sg, nents);
	if (mapped <= 0) {
		kfree(sg);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}

	ata_dma->sg_list = sg;
	ata_dma->sg_nents = mapped;
	ata_dma->direction = direction;

	return DRIVER_OK;
}

inline void ata_bmdma_unmap_sg(AtaDma *ata_dma, void *ptr, uint32_t size) {
	if (ata_dma->sg_list != NULL) {
		dma_unmap_sg(
			ata_dma->dma_device, ata_dma->sg_list, ata_dma->sg_nents);
		kfree(ata_dma->sg_list);
		ata_dma->sg_list = NULL;
		ata_dma->sg_nents = 0;
		ata_dma->direction = DmaNone;
	}
}

inline void ata_bmdma_cancel_sg(AtaDma *ata_dma) {
	if (ata_dma->sg_list != NULL) {
		dma_unmap_sg(
			ata_dma->dma_device, ata_dma->sg_list, ata_dma->sg_nents);
		kfree(ata_dma->sg_list);
		ata_dma->sg_list = NULL;
		ata_dma->sg_nents = 0;
		ata_dma->direction = DmaNone;
	}
}
