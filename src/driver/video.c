#include <driver/video.h>
#include <driver/video_dm.h>
#include <stdint.h>

extern struct VideoDeviceManager video_dm_ext;
void write_pixel_rgbto8(uint8_t *vram, uint8_t r, uint8_t g, uint8_t b) {
	*vram = (r * 6 / 256) + (g * 6 / 256) * 6 + (b * 6 / 256) * 36;
}

void write_pixel_rgbto16(uint8_t *vram, uint8_t r, uint8_t g, uint8_t b) {
	*((uint16_t *)vram) =
		(r * 32 / 256) << 11 | ((g * 64 / 256)) << 5 | (b * 32 / 256);
}

void write_pixel_rgbto24(uint8_t *vram, uint8_t r, uint8_t g, uint8_t b) {
	vram[0] = b;
	vram[1] = g;
	vram[2] = r;
}

void write_pixel_rgbto32(uint8_t *vram, uint8_t r, uint8_t g, uint8_t b) {
	vram[0] = b;
	vram[1] = g;
	vram[2] = r;
	vram[3] = 0;
}

void write_pixel_raw_8(uint8_t *vram, uint32_t color) {
	*vram = color;
}
void write_pixel_raw_16(uint8_t *vram, uint32_t color) {
	*((uint16_t *)vram) = color;
}
void write_pixel_raw_24(uint8_t *vram, uint32_t color) {
	vram[0] = color & 0xff;
	vram[1] = (color >> 8) & 0xff;
	vram[2] = (color >> 16) & 0xff;
}
void write_pixel_raw_32(uint8_t *vram, uint32_t color) {
	*((uint32_t *)vram) = color;
}

FramebufferOps fb_ops_8 = {
	.write_pixel_raw = write_pixel_raw_8,
	.write_pixel_rgb = write_pixel_rgbto8,
};
FramebufferOps fb_ops_16 = {
	.write_pixel_raw = write_pixel_raw_16,
	.write_pixel_rgb = write_pixel_rgbto16,
};
FramebufferOps fb_ops_24 = {
	.write_pixel_raw = write_pixel_raw_24,
	.write_pixel_rgb = write_pixel_rgbto24,
};
FramebufferOps fb_ops_32 = {
	.write_pixel_raw = write_pixel_raw_32,
	.write_pixel_rgb = write_pixel_rgbto32,
};

/**
 * @brief 写像素
 *
 * @param x x坐标
 * @param y y坐标
 * @param color 颜色
 */
void inline write_pixel(
	VideoDevice *video_device, int x, int y, uint32_t color) {
	uint8_t *vram = video_device->framebuffer_address;
	vram += (y * video_device->mode_info.width + x) *
			(video_device->mode_info.bits_per_pixel / 8);
	video_device->framebuffer_ops->write_pixel_raw(vram, color);
}

/**
 * @brief 写像素
 *
 * @param x x坐标
 * @param y y坐标
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void inline write_pixel_rgb(
	VideoDevice *video_device, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
	uint8_t *vram = video_device->framebuffer_address;
	vram += (y * video_device->mode_info.width + x) *
			(video_device->mode_info.bits_per_pixel / 8);
	video_device->framebuffer_ops->write_pixel_rgb(vram, r, g, b);
}

/**
 * @brief 画矩形
 *
 * @param video_device 显示设备
 * @param x x坐标
 * @param y y坐标
 * @param width 宽度
 * @param height 高度
 * @param color 颜色
 */
void draw_rect(
	VideoDevice *video_device, int x, int y, int width, int height, int color) {
	int		 bpp = video_device->mode_info.bytes_per_pixel;
	int		 x0, y0;
	uint8_t *vram = video_device->framebuffer_address +
					(y * video_device->mode_info.width + x) * bpp;
	int delta = (video_device->mode_info.width - width) * bpp;
	for (y0 = 0; y0 < height; y0++) {
		for (x0 = 0; x0 < width; x0++) {
			video_device->framebuffer_ops->write_pixel_raw(vram, color);
			vram += bpp;
		}
		vram += delta;
	}
}

/**
 * @brief 画矩形
 *
 * @param x x坐标
 * @param y y坐标
 * @param width 宽度
 * @param height 高度
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void draw_rect_rgb(
	VideoDevice *video_device, int x, int y, int width, int height, uint8_t r,
	uint8_t g, uint8_t b) {
	int		 bpp = video_device->mode_info.bytes_per_pixel;
	int		 x0, y0;
	uint8_t *vram = video_device->framebuffer_address +
					(y * video_device->mode_info.width + x) * bpp;
	int delta = (video_device->mode_info.width - width) * bpp;
	for (y0 = y; y0 < y + height; y0++) {
		for (x0 = 0; x0 < width; x0++) {
			video_device->framebuffer_ops->write_pixel_rgb(vram, r, g, b);
			vram += bpp;
		}
		vram += delta;
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
void print_word(
	FramebufferOps *ops, VideoModeInfo *mode_info, uint8_t *vram,
	uint8_t *ascii, int color) {
	int		i;
	char	d;
	uint8_t bpp	  = mode_info->bytes_per_pixel;
	int		delta = (mode_info->width - 8) * bpp;
	for (i = 0; i < 16; i++) {
		d = ascii[i];
		if (d & 0x80) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x40) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x20) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x10) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x08) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x04) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x02) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		if (d & 0x01) { ops->write_pixel_raw(vram, color); }
		vram += bpp;
		vram += delta;
	}
}

/**
 * @brief 打印字符
 *
 * @param x x坐标
 * @param y y坐标
 * @param ascii 字符数据(16*8点阵字体)
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void print_word_rgb(
	FramebufferOps *ops, VideoModeInfo *mode_info, uint8_t *vram, char *ascii,
	uint8_t r, uint8_t g, uint8_t b) {
	int		i;
	char	d;
	uint8_t bpp	  = mode_info->bytes_per_pixel;
	int		delta = (mode_info->width - 8) * bpp;
	for (i = 0; i < 16; i++) {
		d = ascii[i];
		if (d & 0x80) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x40) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x20) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x10) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x08) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x04) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x02) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		if (d & 0x01) { ops->write_pixel_rgb(vram, r, g, b); }
		vram += bpp;
		vram += delta;
	}
}
