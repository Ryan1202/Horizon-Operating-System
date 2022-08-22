#ifndef _GRAPHIC_H
#define _GRAPHIC_H

#include <gui/layer.h>
#include <kernel/font.h>

void g_fill_rect(struct layer_s *layer, uint32_t l, uint32_t t, uint32_t r, uint32_t b, uint32_t color);
void g_print_char8(struct layer_s *layer, int x, int y, unsigned char *ascii, unsigned int color);
void g_print_string(struct layer_s *layer, int x, int y, unsigned char *font, char *string, unsigned int color);

#endif