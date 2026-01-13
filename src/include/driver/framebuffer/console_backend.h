#ifndef FB_CONSOLE_BACKEND_H
#define FB_CONSOLE_BACKEND_H

#include "kernel/spinlock.h"
#include <kernel/console.h>
#include <kernel/periodic_task.h>
#include <stdint.h>

#define FB_DEFAULT_FG_COLOR 0xAAAAAA
#define FB_DEFAULT_BG_COLOR 0x000000

typedef struct {
	ConsoleBackend backend;

	struct FrameBufferDevice *fb_device;

	char	**line_ends;
	uint16_t *line_widths;
	char	 *text_buffer;
	char	 *current;
	char	 *last_update;
	int		  buffer_size;

	uint8_t *font;

	uint16_t width;
	uint16_t height;
	uint16_t real_height;

	uint16_t x, y;
	uint16_t last_update_y;
	uint16_t last_update_x;

	uint32_t foreground_color;
	uint32_t default_fg_color;
	uint32_t background_color;
	uint32_t default_bg_color;

	PeriodicTask periodic_task;

	spinlock_t lock;
} FrameBufferConsoleBackend;

void fb_console_backend_init(void *context);
void fb_console_backend_put_string(
	void *context, const char *string, int length);

#endif