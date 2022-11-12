#ifndef _WINDOW_H
#define _WINDOW_H

#include <string.h>
#include <kernel/list.h>
#include <gui/gui.h>

#define WIN_MSG_MOVE	0x02
#define WIN_MSG_LCLICK	0x04
#define WIN_MSG_MCLICK	0x08
#define WIN_MSG_RCLICK	0x10

#define WIN_BACK_COLOR	0x333333
#define WIN_FRONT_COLOR	0xa8a8a8
#define WIN_TITLE_COLOR	0x101010

struct win_s {
	struct gui_s *gui;
	struct layer_s *layer;
	void (*win_handler)(struct win_s *win, uint32_t msgtype, uint32_t value1, uint32_t value2);
	string_t title;
	struct trigger_s triggers;
};

struct win_s *create_win(struct gui_s *gui, int x, int y, int width, int height, int bpp, char *title);
void close_win(struct win_s *win);
void default_win_handler(struct win_s *win, uint32_t msgtype, uint32_t value1, uint32_t value2);

#endif