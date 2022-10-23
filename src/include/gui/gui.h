#ifndef _GUI_H
#define _GUI_H

#include <gui/idmm.h>
#include <kernel/list.h>
#include <stdint.h>

#define GUI_BG_COLOR 0xf7f4ed

struct Rect {
	int l, r, t, b;
};

struct gui_s {
	int width, height;
	int bpp;
	uint8_t new_z;
	uint32_t *frame;
	uint8_t *map;
	uint32_t *z;
	int top;
	struct layer_s **vsb_layer;		//可见图层
	struct layer_s *bg;
	struct layer_s *taskbar;
	struct layer_s *cursor;
	idmm_t idmm;
	struct input_devices *input;
	struct {
		int updated;
		struct Rect oldRect;
		struct Rect newRect;
	}update_area;
	struct {
		int8_t move;
		int dx, dy;
		struct layer_s *layer;
	}move_win;
};

void gui_start(void *arg);
void gui_update_map(struct gui_s *gui, struct Rect *rect);
void gui_update(struct gui_s *gui, int l, int t, int r, int b);
void init_desktop(struct gui_s *gui);

#endif