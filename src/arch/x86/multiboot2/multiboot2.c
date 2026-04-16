#include "multiboot2.h"
#include <drivers/vesa_display.h>
#include <kernel/ards.h>
#include <kernel/font.h>
#include <kernel/page.h>
#include <sections.h>
#include <stdint.h>
#include <types.h>

void __multiboot2 temp_map(uint32_t base_addr) {
	int		pdpt_i = base_addr >> 30;
	size_t *pdpt   = (size_t *)PDPT0_BASE;

	int		pdt_i = (base_addr >> 21) & 0x1ff;
	size_t *pdt	  = (size_t *)PDT0_BASE;
	if (pdpt_i > 0) {
		pdt = (size_t *)PDPT_KBASE; // 临时借用
	}

	pdt[pdt_i] =
		(((size_t)base_addr & ~0x1fffff) | SIGN_HUGE | SIGN_SYS | SIGN_RW |
		 SIGN_P);

	pdpt[pdpt_i] = ((size_t)pdt & ~0xfff) | SIGN_SYS | SIGN_RW | SIGN_P;
}

void __multiboot2 temp_unmap(uint32_t base_addr) {
	int		pdpt_i = base_addr >> 30;
	size_t *pdpt   = (size_t *)PDPT0_BASE;

	int pdt_i = (base_addr >> 21) & 0x1ff;
	if (pdpt_i > 0) {
		pdpt[pdpt_i] = 0;
	} else if (pdt_i > 0) {
		size_t *pdt = (size_t *)PDT0_BASE;
		pdt[pdt_i]	= 0;
	}
}

void __multiboot2 multiboot2_memcpy(void *dst, void *src, size_t size) {
	uint8_t *_dst = dst, *_src = src;
	for (size_t i = 0; i < size; i++) {
		*_dst++ = *_src++;
	}
}

void __multiboot2 multiboot2_loader(uint32_t magic, uint32_t ptr) {
	if (magic != 0x36d76289) {
		while (true)
			;
	}
	temp_map(ptr);

	// 读取multiboot2信息
	uint32_t *p			 = (uint32_t *)(size_t)ptr;
	uint32_t  total_size = *p;

	struct VesaDisplayInfo *vesa_info =
		(struct VesaDisplayInfo *)VESA_INFO_ADDR;

	int i = 0;
	p += 2;
	while (i < total_size) {
		uint32_t type = *p;
		uint32_t size = *(p + 1);
		switch (type) {
		case MBIT_FRAMEBUFFER_INFO: {
			struct framebuffer_tag *fb = (struct framebuffer_tag *)p;
			vesa_info->vram_phy		= (uint8_t *)(size_t)fb->framebuffer_addr;
			vesa_info->width		= fb->framebuffer_width;
			vesa_info->height		= fb->framebuffer_height;
			vesa_info->BitsPerPixel = fb->framebuffer_bpp;
			break;
		}
		case MBIT_VBE_INFO: {
			struct vbe_info_tag *vbe = (struct vbe_info_tag *)p;
			multiboot2_memcpy(
				&vesa_info->vbe_mode_info, vbe->vbe_mode_info,
				sizeof(struct VbeModeInfoBlock));
			multiboot2_memcpy(
				&vesa_info->vbe_control_info, vbe->vbe_control_info,
				sizeof(struct VbeControlInfoBlock));
			break;
		}
		case MBIT_MEM_MAP: {
			struct mem_map_tag *mmap = (struct mem_map_tag *)p;

			int entry_count = (mmap->size - 16) / mmap->entry_size;
			for (int j = 0; j < entry_count; j++) {
				multiboot2_memcpy(
					(void *)ARDS_ADDR + j * sizeof(struct ards),
					(void *)((size_t)p + 16 + j * mmap->entry_size),
					sizeof(struct ards));
			}
			*((uint16_t *)ARDS_NR) = entry_count;
			break;
		}
		case MBIT_END: {
			i = total_size;
			break;
		}
		default:
			break;
		}
		// Tag要对齐8字节
		if (size & 0x07) { size = (size + 8) & (~0x07); }
		i += size;
		size >>= 2;
		p += size;
	}

	temp_unmap(ptr);
}