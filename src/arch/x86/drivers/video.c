/**
 * @file video.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief
 * @version 0.6
 * @date 2020-10
 */
#include <drivers/video.h>
#include <kernel/console.h>
#include <kernel/font.h>
#include <kernel/func.h>
#include <stdint.h>

struct VideoInfo video_info;

/**
 * VIDEO_INFO_ADDR处存着loader保存的显示模式信息
 * VIDEO_INFO_ADDR+0	X分辨率
 * VIDEO_INFO_ADDR+2	Y分辨率
 * VIDEO_INFO_ADDR+4	颜色位数
 * VIDEO_INFO_ADDR+6	显存物理地址
 * VIDEO_INFO_ADDR+10	显示模式的INFO_BLOCK
 *
 * 2024.11.25: VIDEO_INFO_ADDR已废弃，直接在平台初始化阶段赋值video_info
 */

#define SEG_ADDR2LINEAR_ADDR(addr)                              \
	((unsigned int *)(((unsigned int)(addr) >> 12) & 0xffff0) + \
	 ((unsigned int)(addr) & 0xffff))

void init_video() {
	// 因为VRAM地址在启用分页后发生了变化，所以要重新设置
	video_info.vram = (uint8_t *)VRAM_VIR_ADDR;

	printk(
		"Display mode: %d*%d %dbit \n", video_info.width, video_info.height,
		video_info.BitsPerPixel);

	video_info.vbe_mode_info->OemStringPtr =
		SEG_ADDR2LINEAR_ADDR(video_info.vbe_mode_info->OemStringPtr);
	video_info.vbe_mode_info->VideoModePtr =
		SEG_ADDR2LINEAR_ADDR(video_info.vbe_mode_info->VideoModePtr);
	video_info.vbe_mode_info->OemVendorNamePtr =
		SEG_ADDR2LINEAR_ADDR(video_info.vbe_mode_info->OemVendorNamePtr);
	video_info.vbe_mode_info->OemProduceRevPtr =
		SEG_ADDR2LINEAR_ADDR(video_info.vbe_mode_info->OemProduceRevPtr);
	video_info.vbe_mode_info->OemProductNamePtr =
		SEG_ADDR2LINEAR_ADDR(video_info.vbe_mode_info->OemProductNamePtr);

	// 如果是256色模式则设置调色板
	if (video_info.BitsPerPixel == 8) {
		unsigned char table[216 * 3], *p;
		int			  i, j, k, eflags;
		for (i = 0; i < 6; i++) {
			for (j = 0; j < 6; j++) {
				for (k = 0; k < 6; k++) {
					table[(k + j * 6 + i * 36) * 3 + 0] = k * 51;
					table[(k + j * 6 + i * 36) * 3 + 1] = j * 51;
					table[(k + j * 6 + i * 36) * 3 + 2] = i * 51;
				}
			}
		}
		eflags = io_load_eflags();
		io_cli();
		io_out8(0x3c8, 0);
		p = table;
		for (i = 0; i < 216; i++) {
			io_out8(0x3c9, p[0] / 4);
			io_out8(0x3c9, p[1] / 4);
			io_out8(0x3c9, p[2] / 4);
			p += 3;
		}
		io_store_eflags(eflags);
	}
}

/**
 * @brief 显示VBE相关信息
 *
 */
void show_vbeinfo() {
	int i;

	printk("VBE Version:%x\n", video_info.vbe_mode_info->VbeVersion);
	printk("OEMString:%s\n", video_info.vbe_mode_info->OemStringPtr);

	printk("VBE Capabilities:\n");
	printk(
		"    %s\n",
		(video_info.vbe_mode_info->Capabilities & 0x01
			 ? "DAC width is switchable to 8 bits per primary color"
			 : "DAC is fixed width, with 6 bits per primary color"));
	printk(
		"    %s\n", (video_info.vbe_mode_info->Capabilities & 0x02
						 ? "Controller is not VGA compatible"
						 : "Controller is VGA compatible"));
	printk(
		"    %s\n",
		(video_info.vbe_mode_info->Capabilities & 0x04
			 ? "When programming large blocks of information to the RAMDAC"
			 : "Normal RAMDAC operation"));
	printk(
		"    %s\n",
		(video_info.vbe_mode_info->Capabilities & 0x08
			 ? "Hardware stereoscopic signaling supported by controller"
			 : "No hardware stereoscopic signaling support"));
	printk(
		"    %s\n", (video_info.vbe_mode_info->Capabilities & 0x10
						 ? "Stereo signaling supported via VESA EVC"
						 : "Stereo signaling supported via external VESA "
						   "stereo connector"));

	printk("VideoMode:\n");
	for (i = 0;
		 ((unsigned short *)video_info.vbe_mode_info->VideoModePtr)[i] >= 0x100;
		 i++) {
		printk(
			"%x ",
			((unsigned short *)video_info.vbe_mode_info->VideoModePtr)[i]);
	}
	printk("\n");

	printk("VBE totalMemory:%dKB\n", video_info.vbe_mode_info->TotalMemory);
	printk("OEM SoftwareRev:%#x\n", video_info.vbe_mode_info->OemSoftwareRev);
	printk("OEM VendorName:%s\n", video_info.vbe_mode_info->OemVendorNamePtr);
	printk("OEM ProductName:%s\n", video_info.vbe_mode_info->OemProductNamePtr);
	printk("OEM ProduceRev:%s\n", video_info.vbe_mode_info->OemProduceRevPtr);
}