#ifndef _LAYER_H
#define _LAYER_H

#include <gui/gui.h>
#include <kernel/list.h>
#include <stdint.h>

struct layer_s {
	int did;			// 在显示图层中的ID
	int bpp;			// 每像素字节数
	struct Rect rect;	// 图层范围
	int z;				// z轴高度
	uint8_t inc_tp;		// 包含透明颜色
	uint8_t *buffer;	// 图像缓冲区
	struct win_s *win;	// 图层对应的窗口(如果有)
};

struct layer_s *create_layer(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t bpp);
void delete_layer(struct gui_s *gui, struct layer_s *layer);
int show_layer(struct gui_s *gui, struct layer_s *layer);
void hide_layer(struct gui_s * gui, struct layer_s *layer);
void move_layer(struct gui_s *gui, struct layer_s *layer, int new_x, int new_y);
void layer_set_z(struct gui_s *gui, struct layer_s *layer, int32_t z);

#endif