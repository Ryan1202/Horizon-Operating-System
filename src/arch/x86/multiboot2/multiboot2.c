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
		video_info.vram			= (uint8_t *)0xe0000000;
		video_info.width		= 1024;
		video_info.height		= 768;
		video_info.BitsPerPixel = 32;
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
			video_info.vram			   = (uint8_t *)fb->framebuffer_addr[0];
			video_info.width		   = fb->framebuffer_width;
			video_info.height		   = fb->framebuffer_height;
			video_info.BitsPerPixel	   = fb->framebuffer_bpp;
			break;
		}
		case MBIT_VBE_INFO: {
			struct vbe_info_tag *vbe = (struct vbe_info_tag *)p;
			video_info.vbe_mode_info =
				(struct vbe_mode_info_block *)vbe->vbe_mode_info;
			video_info.vbe_conrtol_info =
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