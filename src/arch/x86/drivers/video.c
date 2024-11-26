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

struct video_info VideoInfo;

/**
 * VIDEO_INFO_ADDR处存着loader保存的显示模式信息
 * VIDEO_INFO_ADDR+0	X分辨率
 * VIDEO_INFO_ADDR+2	Y分辨率
 * VIDEO_INFO_ADDR+4	颜色位数
 * VIDEO_INFO_ADDR+6	显存物理地址
 * VIDEO_INFO_ADDR+10	显示模式的INFO_BLOCK
 *
 * 2024.11.25: VIDEO_INFO_ADDR已废弃，直接在平台初始化阶段赋值VideoInfo
 */

#define SEG_ADDR2LINEAR_ADDR(addr)                              \
	((unsigned int *)(((unsigned int)(addr) >> 12) & 0xffff0) + \
	 ((unsigned int)(addr) & 0xffff))

void init_video() {
	// 因为VRAM地址在启用分页后发生了变化，所以要重新设置
	VideoInfo.vram = (uint8_t *)VRAM_VIR_ADDR;

	printk(
		"Display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height,
		VideoInfo.BitsPerPixel);

	VideoInfo.vbe_mode_info->OemStringPtr =
		SEG_ADDR2LINEAR_ADDR(VideoInfo.vbe_mode_info->OemStringPtr);
	VideoInfo.vbe_mode_info->VideoModePtr =
		SEG_ADDR2LINEAR_ADDR(VideoInfo.vbe_mode_info->VideoModePtr);
	VideoInfo.vbe_mode_info->OemVendorNamePtr =
		SEG_ADDR2LINEAR_ADDR(VideoInfo.vbe_mode_info->OemVendorNamePtr);
	VideoInfo.vbe_mode_info->OemProduceRevPtr =
		SEG_ADDR2LINEAR_ADDR(VideoInfo.vbe_mode_info->OemProduceRevPtr);
	VideoInfo.vbe_mode_info->OemProductNamePtr =
		SEG_ADDR2LINEAR_ADDR(VideoInfo.vbe_mode_info->OemProductNamePtr);

	// 如果是256色模式则设置调色板
	if (VideoInfo.BitsPerPixel == 8) {
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

	printk("VBE Version:%x\n", VideoInfo.vbe_mode_info->VbeVersion);
	printk("OEMString:%s\n", VideoInfo.vbe_mode_info->OemStringPtr);

	printk("VBE Capabilities:\n");
	printk(
		"    %s\n",
		(VideoInfo.vbe_mode_info->Capabilities & 0x01
			 ? "DAC width is switchable to 8 bits per primary color"
			 : "DAC is fixed width, with 6 bits per primary color"));
	printk(
		"    %s\n", (VideoInfo.vbe_mode_info->Capabilities & 0x02
						 ? "Controller is not VGA compatible"
						 : "Controller is VGA compatible"));
	printk(
		"    %s\n",
		(VideoInfo.vbe_mode_info->Capabilities & 0x04
			 ? "When programming large blocks of information to the RAMDAC"
			 : "Normal RAMDAC operation"));
	printk(
		"    %s\n",
		(VideoInfo.vbe_mode_info->Capabilities & 0x08
			 ? "Hardware stereoscopic signaling supported by controller"
			 : "No hardware stereoscopic signaling support"));
	printk(
		"    %s\n", (VideoInfo.vbe_mode_info->Capabilities & 0x10
						 ? "Stereo signaling supported via VESA EVC"
						 : "Stereo signaling supported via external VESA "
						   "stereo connector"));

	printk("VideoMode:\n");
	for (i = 0;
		 ((unsigned short *)VideoInfo.vbe_mode_info->VideoModePtr)[i] >= 0x100;
		 i++) {
		printk(
			"%x ",
			((unsigned short *)VideoInfo.vbe_mode_info->VideoModePtr)[i]);
	}
	printk("\n");

	printk("VBE totalMemory:%dKB\n", VideoInfo.vbe_mode_info->TotalMemory);
	printk("OEM SoftwareRev:%#x\n", VideoInfo.vbe_mode_info->OemSoftwareRev);
	printk("OEM VendorName:%s\n", VideoInfo.vbe_mode_info->OemVendorNamePtr);
	printk("OEM ProductName:%s\n", VideoInfo.vbe_mode_info->OemProductNamePtr);
	printk("OEM ProduceRev:%s\n", VideoInfo.vbe_mode_info->OemProduceRevPtr);
}

/**
 * @brief 写像素
 *
 * @param x x坐标
 * @param y y坐标
 * @param color 颜色
 */
void write_pixel(int x, int y, unsigned int color) {
	unsigned char r, g, b;
	r = (unsigned char)(color >> 16);
	g = (unsigned char)(color >> 8);
	b = (unsigned char)color;
	unsigned char *vram =
		(unsigned char *)(VideoInfo.vram + (y * VideoInfo.width + x) *
											   (VideoInfo.BitsPerPixel / 8));
	if (VideoInfo.BitsPerPixel == 32) {
		vram[0] = color & 0xff;
		vram[1] = (color >> 8) & 0xff;
		vram[2] = (color >> 16) & 0xff;
	} else if (VideoInfo.BitsPerPixel == 24) {
		vram[0] = color & 0xff;
		vram[1] = (color >> 8) & 0xff;
		vram[2] = (color >> 16) & 0xff;
	} else if (VideoInfo.BitsPerPixel == 16) {
		vram[0] = ((g * 63 / 255) & 0x7) << 5 | (b * 31 / 255);
		vram[1] = (r * 31 / 255) << 3 | ((g * 63 / 255) & 0x38) >> 3;
	} else if (VideoInfo.BitsPerPixel == 8) {
		vram[0] = (r / 51) + (g / 51) * 6 + (b / 51) * 36;
	}
}

/**
 * @brief 画矩形
 *
 * @param x x坐标
 * @param y y坐标
 * @param width 宽度
 * @param height 高度
 * @param color 颜色
 */
void draw_rect(int x, int y, int width, int height, int color) {
	/*wide-video wide*/
	int x0, y0;
	for (y0 = 0; y0 < height; y0++) {
		for (x0 = 0; x0 < width; x0++) {
			write_pixel(x + x0, y + y0, color);
		}
	}
}

/**
 * @brief 打印字符
 *
 * @param x x坐标
 * @param y y坐标
 * @param ascii 字符数据(16*8点阵字体)
 * @param color 颜色
 */
void print_word(int x, int y, unsigned char *ascii, unsigned int color) {
	int	 i;
	char d;
	for (i = 0; i < 16; i++) {
		// vram = (unsigned char *)(VideoInfo.vram + ((y+i)*VideoInfo.width +
		// x)*(VideoInfo.BitsPerPixel/8));
		d = ascii[i];
		if (d & 0x80) { write_pixel(x + 0, y + i, color); }
		if (d & 0x40) { write_pixel(x + 1, y + i, color); }
		if (d & 0x20) { write_pixel(x + 2, y + i, color); }
		if (d & 0x10) { write_pixel(x + 3, y + i, color); }
		if (d & 0x08) { write_pixel(x + 4, y + i, color); }
		if (d & 0x04) { write_pixel(x + 5, y + i, color); }
		if (d & 0x02) { write_pixel(x + 6, y + i, color); }
		if (d & 0x01) { write_pixel(x + 7, y + i, color); }
	}
}

/**
 * @brief 打印字符串
 *
 * @param x x坐标
 * @param y y坐标
 * @param color 颜色
 * @param font 字体
 * @param string 字符串
 */
void print_string(
	int x, int y, unsigned int color, unsigned char *font, char *string) {
	while (*string != 0) {
		print_word(x, y, font + (*string) * 16, color);
		string++;
		x += 9;
	}
}