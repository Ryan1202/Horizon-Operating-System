#include "multiboot2.h"
#include <drivers/video.h>
#include <kernel/ards.h>
#include <kernel/font.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

void multiboot2_loader(uint32_t eax, uint32_t ebx) {
	if (eax != 0x36d76289) {
		// 尝试输出
		VideoInfo.vram		   = (uint8_t *)0xe0000000;
		VideoInfo.width		   = 1024;
		VideoInfo.height	   = 768;
		VideoInfo.BitsPerPixel = 32;
		print_string(
			0, 0, 0xffffff, font16,
			"Not booted by a multiboot2-compliant bootloader.");
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
		case MBIT_FRAMEBUFER_INFO: {
			struct framebuffer_tag *fb = (struct framebuffer_tag *)p;
			VideoInfo.vram			   = (uint8_t *)fb->framebuffer_addr[0];
			VideoInfo.width			   = fb->framebuffer_width;
			VideoInfo.height		   = fb->framebuffer_height;
			VideoInfo.BitsPerPixel	   = fb->framebuffer_bpp;
			break;
		}
		case MBIT_VBE_INFO: {
			struct vbe_info_tag *vbe = (struct vbe_info_tag *)p;
			VideoInfo.vbe_mode_info =
				(struct vbe_mode_info_block *)vbe->vbe_mode_info;
			VideoInfo.vbe_conrtol_info =
				(struct vbe_control_info_block *)vbe->vbe_control_info;
			break;
		}
		case MBIT_MEM_MAP: {
			struct mem_map_tag *mmap = (struct mem_map_tag *)p;

			int entry_count = (mmap->size - 16) / mmap->entry_size;
			for (int j = 0; j < entry_count; j++) {
				memcpy(
					(void *)ards_addr + j * sizeof(struct ards),
					(void *)p + 16 + j * mmap->entry_size, sizeof(struct ards));
			}
			*((uint16_t *)ards_nr_addr) = entry_count;
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
	print_string(0, 0, 0xc0c0c0, font16, "Starting up by multiboot2 success!");
}