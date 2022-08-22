#ifndef _WINDOW_H
#define _WINDOW_H

#include <string.h>

struct win_s {
	struct layer_s *layer;
	string_t title;
};

struct win_s *create_win(int x, int y, int width, int height, int bpp, char *title);

#endif