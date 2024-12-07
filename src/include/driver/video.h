#ifndef _DRIVER_VIDEO_H
#define _DRIVER_VIDEO_H
// TODO: video.h改名
#include "stdint.h"

typedef struct FramebufferOps {
	void (*write_pixel_raw)(uint8_t *vram, uint32_t color);
	void (*write_pixel_rgb)(uint8_t *vram, uint8_t r, uint8_t g, uint8_t b);
} FramebufferOps;

struct VideoDevice;
struct VideoModeInfo;
void inline write_pixel(
	struct VideoDevice *video_device, int x, int y, uint32_t color);
void inline write_pixel_rgb(
	struct VideoDevice *video_device, int x, int y, uint8_t r, uint8_t g,
	uint8_t b);
void draw_rect(
	struct VideoDevice *video_device, int x, int y, int width, int height,
	int color);
void draw_rect_rgb(
	struct VideoDevice *video_device, int x, int y, int width, int height,
	uint8_t r, uint8_t g, uint8_t b);
void print_word(
	FramebufferOps *ops, struct VideoModeInfo *mode_info, uint8_t *vram,
	uint8_t *ascii, int color);
void print_word_rgb(
	FramebufferOps *ops, struct VideoModeInfo *mode_info, uint8_t *vram,
	char *ascii, uint8_t r, uint8_t g, uint8_t b);

extern FramebufferOps fb_ops_8;
extern FramebufferOps fb_ops_16;
extern FramebufferOps fb_ops_24;
extern FramebufferOps fb_ops_32;
#endif