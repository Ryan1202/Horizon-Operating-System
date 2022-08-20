#ifndef _CURSOR_H
#define _CURSOR_H

#include <gui/layer.h>
#include <gui/input.h>

#define CURSOR_WIDTH 23
#define CURSOR_HEIGHT 23

void init_cursor(struct gui_s *gui);
void set_cursor_mode(struct layer_s *layer, enum cursor_status status);

#endif