#include "multiboot2.h"
#include <drivers/vesa_display.h>
#include <kernel/ards.h>
#include <kernel/font.h>
#include <sections.h>
#include <stdint.h>
#include <types.h>

extern struct VesaDisplayInfo vesa_display_info;

void __multiboot2 multiboot2_memcpy(void *dst, void *src, size_t size) {
	uint8_t *_dst = dst, *_src = src;
	for (size_t i = 0; i < size; i++) {
		*_dst++ = *_src++;
	}
}

void __multiboot2 multiboot2_loader(uint32_t eax, uint32_t ebx) {
	if (eax != 0x36d76289) {
		while (true)
			;
	}
	// 读取multiboot2信息
	uint32_t *p			 = (uint32_t *)ebx;
	uint32_t  total_size = *p;

	int i = 0;
	p += 2;
	while (i < total_size) {
		uint32_t type = *p;
		uint32_t size = *(p + 1);
		switch (type) {
		case MBIT_FRAMEBUFFER_INFO: {
			struct framebuffer_tag *fb	   = (struct framebuffer_tag *)p;
			vesa_display_info.vram		   = (uint8_t *)fb->framebuffer_addr[0];
			vesa_display_info.width		   = fb->framebuffer_width;
			vesa_display_info.height	   = fb->framebuffer_height;
			vesa_display_info.BitsPerPixel = fb->framebuffer_bpp;
			break;
		}
		case MBIT_VBE_INFO: {
			struct vbe_info_tag *vbe = (struct vbe_info_tag *)p;
			vesa_display_info.vbe_mode_info =
				(struct VbeModeInfoBlock *)vbe->vbe_mode_info;
			vesa_display_info.vbe_conrtol_info =
				(struct VbeControlInfoBlock *)vbe->vbe_control_info;
			break;
		}
		case MBIT_MEM_MAP: {
			struct mem_map_tag *mmap = (struct mem_map_tag *)p;

			int entry_count = (mmap->size - 16) / mmap->entry_size;
			for (int j = 0; j < entry_count; j++) {
				multiboot2_memcpy(
					(void *)ARDS_ADDR + j * sizeof(struct ards),
					(void *)p + 16 + j * mmap->entry_size, sizeof(struct ards));
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
}