#ifndef _LAYER_H
#define _LAYER_H

#include <gui/gui.h>
#include <kernel/list.h>
#include <stdint.h>

struct layer_s {
	int did;			// 在显示图层中的ID
	int bpp;			// 每像素字节数
	int x;				// x坐标
	int y;				// y坐标
	int width;			// 宽度
	int height;			// 高度
	int z;				// z轴高度
	uint8_t opacity;	// 不透明度
	uint8_t *buffer;	// 图像缓冲区
};

struct layer_s *create_layer(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t bpp);
int show_layer(struct gui_s *gui, struct layer_s *layer);
void move_layer(struct gui_s *gui, struct layer_s *layer, int new_x, int new_y);

#endif