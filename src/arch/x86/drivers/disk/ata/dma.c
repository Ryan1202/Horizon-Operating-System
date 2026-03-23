#include "include/dma.h"
#include "include/ide.h"
#include "include/ide_controller.h"
#include "kernel/driver.h"
#include "kernel/list.h"
#include "kernel/memory.h"
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
	DmaSegment				 *seg;

	int		 i = 0, size, left_size;
	uint32_t addr, offset;
	list_for_each_owner (seg, &ata_dma->segment_lh, list) {
		left_size = seg->size;
		addr	  = seg->addr;
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
	prds[i - 1].sign = BIT(15);

	io_out_dword(channel->bmide + IDE_REG_BM_PRDT, ata_dma->prdt_phy_addr);

	uint8_t data;
	data = io_in_byte(channel->bmide + IDE_REG_BM_COMMAND);
	data = (rw == 0) ? BIN_EN(data, IDE_BMCMD_READ_WRITE)
					 : BIN_DIS(data, IDE_BMCMD_READ_WRITE);
	io_out_byte(channel->bmide + IDE_REG_BM_COMMAND, data);

	// 清除 BM 状态 (W1C: 只写入要清除的位，不能读-改-写，否则会把ACTIVE等只读位写入)
	io_out_byte(channel->bmide + IDE_REG_BM_STATUS, IDE_BMSTATUS_INT | IDE_BMSTATUS_ERROR);
}

DriverResult ata_bmdma_map_buffer(AtaDma *ata_dma, void *ptr, uint32_t size) {
	size_t addr;
	addr = (size_t)ptr;

	if (addr & 3) {
		// 申请一个小的临时缓冲区处理未对齐的部分
		int	  size	  = 32 - (addr & 0x1f);
		void *tmp_buf = kmalloc(size);
		if (tmp_buf == NULL) { return DRIVER_ERROR_OUT_OF_MEMORY; }
		memcpy(tmp_buf, ptr, size);

		DmaSegment *seg = kmalloc(sizeof(DmaSegment));
		seg->vaddr		= (size_t)tmp_buf;
		seg->addr		= vir2phy((uint32_t)tmp_buf);
		seg->size		= size;
		list_add_tail(&seg->list, &ata_dma->segment_lh);
		ptr += size;
	}

	dma_split_mem(&ata_dma->segment_lh, ptr, size, ata_dma->max_segment_size);
	return DRIVER_OK;
}

void ata_bmdma_unmap_buffer(AtaDma *ata_dma, void *ptr, uint32_t size) {
	DmaSegment *seg, *next;
	size_t		start_ptr = (size_t)ptr;
	size_t		end_ptr	  = start_ptr + size;
	void	   *addr	  = ptr;

	list_for_each_owner_safe (seg, next, &ata_dma->segment_lh, list) {
		if (start_ptr > seg->vaddr || seg->vaddr >= end_ptr) {
			memcpy(addr, (void *)seg->vaddr, seg->size);
			// 释放临时缓冲区
			kfree((void *)seg->vaddr);
		}
		addr += seg->size;
		list_del(&seg->list);
		kfree(seg);
	}
}
