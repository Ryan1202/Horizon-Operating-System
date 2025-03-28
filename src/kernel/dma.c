#include "kernel/driver.h"
#include "kernel/list.h"
#include <kernel/dma.h>
#include <kernel/page.h>
#include <math.h>
#include <stdint.h>

DriverResult dma_split_mem(
	list_t *lh, void *ptr, uint32_t size, int max_segment_size) {
	DmaSegment *seg;
	size_t		addr	 = (size_t)ptr;
	size_t		end_addr = addr + size;
	size_t		phy_addr, old_phy_page;
	int			seg_size = 0, tmp_size = 0;
	size_t		seg_start;
	size_t		seg_start_vaddr;
	size_t		page_addr;

	// TODO:未对齐的页
	for (; addr < end_addr; addr += tmp_size) {
		phy_addr  = vir2phy(addr);
		page_addr = phy_addr & ~(PAGE_SIZE - 1);
		tmp_size  = PAGE_SIZE - (phy_addr & (PAGE_SIZE - 1));
		tmp_size  = MIN(tmp_size, size);

		if (seg_size == 0) {
			seg_start		= phy_addr;
			seg_start_vaddr = addr;
			seg_size		= tmp_size;
		} else {
			if (page_addr == old_phy_page + PAGE_SIZE &&
				seg_size + tmp_size < max_segment_size) {
				seg_size += tmp_size;
			} else {
				// 创建新的请求
				seg = kmalloc(sizeof(DmaSegment));
				if (seg == NULL) { return DRIVER_RESULT_OUT_OF_MEMORY; }

				// 设置新请求的参数
				seg->vaddr = seg_start_vaddr;
				seg->addr  = seg_start;
				seg->size  = seg_size;
				list_add_tail(&seg->list, lh);

				seg_start_vaddr = addr;
				seg_start		= phy_addr;
				seg_size		= tmp_size;
			}
		}
		old_phy_page = page_addr;
	}
	if (seg_size > 0) {
		seg = kmalloc(sizeof(DmaSegment));
		if (seg == NULL) { return DRIVER_RESULT_OUT_OF_MEMORY; }

		// 设置新请求的参数
		seg->vaddr = seg_start_vaddr;
		seg->addr  = seg_start;
		seg->size  = seg_size;
		list_add_tail(&seg->list, lh);
	}
	return DRIVER_RESULT_OK;
}