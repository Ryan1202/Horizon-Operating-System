#include <driver/framebuffer/console_backend.h>
#include <driver/framebuffer/fb.h>
#include <driver/framebuffer/fb_dm.h>
#include <kernel/console.h>
#include <kernel/font.h>
#include <kernel/memory.h>
#include <kernel/periodic_task.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

void fb_console_backend_update(void *arg);

void fb_console_backend_init(void *context) {
	FrameBufferDevice		  *fb_device = context;
	FrameBufferConsoleBackend *backend	 = &fb_device->console_backend;

	backend->fb_device	 = fb_device;
	backend->font		 = font16;
	backend->width		 = fb_device->mode_info.width / 10;
	backend->real_height = fb_device->mode_info.height / 16;
	backend->height		 = backend->real_height + 1; // 多出4行用于滚屏

	backend->buffer_size = backend->width * backend->height;
	backend->text_buffer = kmalloc(backend->buffer_size);
	backend->current	 = backend->text_buffer;
	backend->last_update = backend->text_buffer;
	backend->line_ends	 = kmalloc(backend->height * sizeof(char *));
	backend->line_widths = kmalloc(backend->height * sizeof(uint16_t));
	for (int i = 0; i < backend->height; i++) {
		backend->line_ends[i]	= backend->text_buffer;
		backend->line_widths[i] = 0;
	}

	backend->x = backend->y = 0;
	backend->last_update_y	= 0;
	backend->last_update_x	= 0;

	backend->default_fg_color = FB_DEFAULT_FG_COLOR;
	backend->default_bg_color = FB_DEFAULT_BG_COLOR;
	backend->foreground_color = backend->default_fg_color;
	backend->background_color = backend->default_bg_color;

	backend->periodic_task.func = fb_console_backend_update;
	backend->periodic_task.arg	= backend;
	periodic_task_add(&backend->periodic_task);

	spinlock_init(&backend->lock);
}

void fb_console_backend_scroll(FrameBufferConsoleBackend *backend, int lines) {
	FrameBufferDevice *fb_device = backend->fb_device;

	int		 bpp		 = fb_device->mode_info.bytes_per_pixel;
	int		 line_length = fb_device->mode_info.width * bpp;
	uint8_t *vram		 = fb_device->framebuffer_address;
	uint8_t *src_vram	 = vram + (lines * 16 * line_length);
	uint8_t *dst_vram	 = vram;

	int width, offset = backend->line_ends[lines - 1] - backend->text_buffer;
	width = MAX(backend->line_widths[0], backend->line_widths[lines]);
	width *= 10 * bpp;
	for (int j = 0; j < 16; j++) {
		memcpy(dst_vram, src_vram, width);
		src_vram += line_length;
		dst_vram += line_length;
	}
	for (int i = 1; i < backend->real_height - lines; i++) {
		width = MAX(backend->line_widths[i], backend->line_widths[i + lines]);
		width *= 10 * bpp;
		for (int j = 0; j < 16; j++) {
			memcpy(dst_vram, src_vram, width);
			src_vram += line_length;
			dst_vram += line_length;
		}
	}
	for (int i = backend->real_height - lines; i < backend->real_height; i++) {
		draw_rect(
			fb_device, 0, i * 16, backend->line_widths[i] * 10, 16,
			backend->background_color);
	}
	memcpy(
		backend->text_buffer, backend->text_buffer + offset,
		backend->buffer_size - offset);
	memcpy(
		&backend->line_widths[0], &backend->line_widths[lines],
		(backend->height - lines) * sizeof(uint16_t));
	for (int i = lines; i < backend->height; i++) {
		backend->line_ends[i - lines] = backend->line_ends[i] - offset;
	}
	for (int i = backend->height - lines; i < backend->height; i++) {
		backend->line_ends[i]	= backend->text_buffer + backend->buffer_size;
		backend->line_widths[i] = 0;
	}
	backend->y -= lines;
	backend->last_update_y -= lines;
	backend->current -= offset;
	backend->last_update -= offset;
}

void fb_console_backend_control(
	FrameBufferConsoleBackend *backend, char *current) {
	const uint32_t color_map1[8] = {
		0x000000, 0xaa0000, 0x00aa00, 0xaa5500,
		0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
	};
	const uint32_t color_map2[8] = {
		0x555555, 0xff5555, 0x55ff55, 0xffff55,
		0x5555ff, 0xff55ff, 0x55ffff, 0xffffff,
	};
	int param1 = -1, param2 = -1;

	if (*current >= '0' && *current <= '9') {
		param1 = *current - '0';
		current++;
	}
	while (*current >= '0' && *current <= '9') {
		param1 = param1 * 10 + (*current - '0');
		current++;
	}

	if (*current == ';') {
		current++;
		if (*current >= '0' && *current <= '9') {
			param2 = *current - '0';
			current++;
		}
		while (*current >= '0' && *current <= '9') {
			param2 = param2 * 10 + (*current - '0');
			current++;
		}
	}

	switch (*current++) {
	case 'm': {
		if (param1 == -1) {
			backend->foreground_color = backend->default_fg_color;
			backend->background_color = backend->default_bg_color;
		} else if (param1 == 1) {
			if (param2 >= 30 && param2 <= 37) {
				backend->foreground_color = color_map2[(param2 - 30) & 7];
			} else if (param2 >= 40 && param2 <= 47) {
				backend->background_color = color_map2[(param2 - 40) & 7];
			}
		} else if (param1 >= 30 && param1 <= 37) {
			backend->foreground_color = color_map1[(param1 - 30) & 7];
		} else if (param1 >= 40 && param1 <= 47) {
			backend->background_color = color_map1[(param1 - 40) & 7];
		}
	}
	default:
		break;
	}
	backend->last_update = current;
}

void fb_console_backend_update(void *arg) {
	FrameBufferConsoleBackend *backend = arg;

	int status = spin_try_lock_irqsave(&backend->lock);
	if (status == 0) return;

	FrameBufferDevice *fb_device = backend->fb_device;
	int				   bpp		= backend->fb_device->mode_info.bytes_per_pixel;
	int				   screen_w = backend->fb_device->mode_info.width;
	int				   line_length = screen_w * bpp;
	uint8_t			  *vram		   = backend->fb_device->framebuffer_address;
	uint8_t *cur_vram = vram + (backend->last_update_y * 16 * screen_w +
								backend->last_update_x * 10) *
								   bpp;

	char *current = backend->last_update;
	if (backend->y >= backend->real_height) {
		fb_console_backend_scroll(
			backend, backend->y - backend->real_height + 1);
		cur_vram = vram + (backend->last_update_y * 16 * screen_w +
						   backend->last_update_x * 10) *
							  bpp;
		current = backend->last_update;
	}
	while (current < backend->current) {
		if (current == backend->line_ends[backend->last_update_y]) {
			backend->last_update_y++;
			backend->last_update_x = 0;
			cur_vram = vram + backend->last_update_y * 16 * line_length;
			continue;
		}
		if (current[0] == '\033') {
			if (current[1] == '[') {
				current += 2;
				fb_console_backend_control(backend, current);
				current = backend->last_update;
				continue;
			}
		}
		draw_rect(
			fb_device, backend->last_update_x * 10, backend->last_update_y * 16,
			10, 16, backend->background_color);
		print_word(
			fb_device->framebuffer_ops, &fb_device->mode_info, cur_vram,
			backend->font + (*current) * 16, backend->foreground_color);
		cur_vram += 10 * bpp;
		backend->last_update_x++;
		current++;
	}
	if (current == backend->line_ends[backend->last_update_y]) {
		backend->last_update_y++;
	}
	backend->last_update   = backend->current;
	backend->last_update_y = backend->y;
	backend->last_update_x = backend->x;

	spin_unlock_irqrestore(&backend->lock, status);
}

void fb_console_backend_put_string(
	void *context, const char *string, int length) {
	FrameBufferDevice		  *fb_device = context;
	FrameBufferConsoleBackend *backend	 = &fb_device->console_backend;

	spin_lock(&backend->lock);
	for (int i = 0; i < length; i++) {
		bool new_line = false;
		if (string[i] == '\033') {
			bool flag			= true;
			*backend->current++ = string[i++];
			*backend->current++ = string[i++];
			while (flag) {
				*backend->current++ = string[i];
				if (string[i] >= 'a' && string[i] <= 'z') flag = false;
				if (string[i] >= 'A' && string[i] <= 'Z') flag = false;
				i++;
			}
			i--;
		} else if (string[i] == '\n') {
			new_line = true;
		} else if (string[i] == '\b') {
			backend->current--;

			backend->x = MAX(backend->x - 1, 0);
			backend->line_widths[backend->y] =
				MAX(backend->line_widths[backend->y] - 1, 0);
			if (backend->y == backend->last_update_y) {
				backend->last_update_x = backend->x;
			}
		} else if (string[i] == '\t') {
			do {
				*backend->current++ = ' ';
				backend->x++;
				backend->line_widths[backend->y]++;
			} while (backend->x & 3);
			backend->line_widths[backend->y] =
				MIN(backend->line_widths[backend->y], backend->width);
			if (backend->x >= backend->width) new_line = true;
		} else {
			*backend->current++ = string[i];
			backend->x++;
			backend->line_widths[backend->y]++;
			if (backend->x >= backend->width) new_line = true;
		}
		if (new_line) {
			backend->line_ends[backend->y++] = backend->current;

			backend->x = 0;
			if (backend->y >= backend->height) {
				// 超过最大行数，滚屏
				spin_unlock(&backend->lock);
				fb_console_backend_scroll(backend, 1);
				spin_lock(&backend->lock);
			}
		}
	}
	spin_unlock(&backend->lock);
	if (list_empty(&thread_all)) { fb_console_backend_update(backend); }
}
