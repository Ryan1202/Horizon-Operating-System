#ifndef _DRIVER_FRAMEBUFFER_H
#define _DRIVER_FRAMEBUFFER_H

#include "stdint.h"

typedef struct FramebufferOps {
	void (*write_pixel_raw)(uint8_t *vram, uint32_t color);
	void (*write_pixel_rgb)(uint8_t *vram, uint8_t r, uint8_t g, uint8_t b);
} FramebufferOps;

struct FrameBufferDevice;
struct FrameBufferModeInfo;
void inline write_pixel(
	struct FrameBufferDevice *fb_device, int x, int y, uint32_t color);
void inline write_pixel_rgb(
	struct FrameBufferDevice *fb_device, int x, int y, uint8_t r, uint8_t g,
	uint8_t b);
void draw_rect(
	struct FrameBufferDevice *fb_device, int x, int y, int width, int height,
	int color);
void draw_rect_rgb(
	struct FrameBufferDevice *fb_device, int x, int y, int width, int height,
	uint8_t r, uint8_t g, uint8_t b);
void print_word(
	FramebufferOps *ops, struct FrameBufferModeInfo *mode_info, uint8_t *vram,
	uint8_t *ascii, int color);
void print_word_rgb(
	FramebufferOps *ops, struct FrameBufferModeInfo *mode_info, uint8_t *vram,
	char *ascii, uint8_t r, uint8_t g, uint8_t b);

extern FramebufferOps fb_ops_8;
extern FramebufferOps fb_ops_16;
extern FramebufferOps fb_ops_24;
extern FramebufferOps fb_ops_32;
#endif