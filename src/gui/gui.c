/**
 * @file gui.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief GUI主程序
 * @version 0.1
 * @date 2022-08-12
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <drivers/video.h>
#include <gui/gui.h>
#include <gui/layer.h>
#include <gui/graphic.h>
#include <gui/input.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <kernel/fifo.h>
#include <drivers/pit.h>
#include <math.h>
#include <string.h>

void gui_main(struct gui_s *gui);

/**
 * @brief GUI主程序入口
 * 
 * @param arg 保留
 */
void gui_start(void *arg)
{
	struct gui_s *gui = kmalloc(sizeof(struct gui_s));
	
	gui->width = VideoInfo.width;
	gui->height = VideoInfo.height;
	gui->bpp = VideoInfo.BitsPerPixel/8;
	
	gui->frame = kmalloc(gui->width * gui->height * gui->bpp);
	gui->map = kmalloc(gui->width * gui->height);
	gui->vsb_layer = kmalloc(256);
	gui->z = kmalloc(gui->width * gui->height * sizeof(uint32_t));
	memset(gui->map, 0, gui->width * gui->height);
	memset(gui->vsb_layer, 0, 256);
	gui->top = -1;
	gui->update_area.updated = 1;
	gui->update_area.r = gui->update_area.b = 0;
	gui->update_area.l = gui->width;
	gui->update_area.t = gui->height;
	
	init_idmm(&gui->idmm, 255);
	
	init_cursor(gui);
	init_input(gui);
	init_desktop(gui);
	
	gui_main(gui);
}

void gui_main(struct gui_s *gui)
{
	struct fifo fifo;
	int buf[2];
	fifo_init(&fifo, 2, buf);
	struct timer *timer = timer_alloc();
	timer_init(timer, &fifo, 1);
	timer_settime(timer, 1);
	for (;;)
	{
		input_handler(gui);
		if (fifo_status(&fifo) > 0)
		{
			fifo_get(&fifo);
			if (gui->update_area.updated == 0)
			{
				gui_update(gui, gui->update_area.l, gui->update_area.t, gui->update_area.r, gui->update_area.b);
				gui->update_area.updated = 1;
				gui->update_area.r = gui->update_area.b = 0;
				gui->update_area.l = gui->width;
				gui->update_area.t = gui->height;
			}
			timer_settime(timer, 1);
		}
		io_hlt();
	}
}

/**
 * @brief 刷新map
 * 
 * @param gui gui描述结构
 * @param l 左坐标
 * @param t 上坐标
 * @param r 右坐标
 * @param b 下坐标
 */
void gui_update_map(struct gui_s *gui, int l, int t, int r, int b)
{
	int h, x, y, bx0, by0, bx1, by1, vx, vy;
	struct layer_s *layer;
	l = MAX(l, 0);
	t = MAX(t, 0);
	r = MIN(r, gui->width);
	b = MIN(b, gui->height);
	for (h = 0; h <= gui->top; h++)
	{
		layer = gui->vsb_layer[gui->z[h]];
		bx0 = MAX(l - layer->x, 0);
		bx1 = MIN(r - layer->x, layer->width);
		by0 = MAX(t - layer->y, 0);
		by1 = MIN(b - layer->y, layer->height);
		for (y = by0; y < by1; y++)
		{
			vy = y + layer->y;
			for (x = bx0; x < bx1; x++)
			{
				vx = x + layer->x;
				if ((((uint32_t *)layer->buffer)[(y*layer->width + x)*layer->bpp/4] & 0xff000000) == 0)
				{
					gui->map[vy * gui->width + vx] = layer->did;
				}
			}
		}
	}
	gui->update_area.updated = 0;
	gui->update_area.l = MIN(gui->update_area.l, l);
	gui->update_area.t = MIN(gui->update_area.t, t);
	gui->update_area.r = MAX(gui->update_area.r, r);
	gui->update_area.b = MAX(gui->update_area.b, b);
	return;
}

/**
 * @brief 刷新画面
 * 
 * @param gui gui描述结构
 * @param l 左坐标
 * @param t 上坐标
 * @param r 右坐标
 * @param b 下坐标
 */
void gui_update(struct gui_s *gui, int l, int t, int r, int b)
{
	int x, y;
	uint32_t pos;
	uint32_t color;
	struct layer_s *layer;
	for (y = t; y < b; y++)
	{
		for (x = l; x < r; x++)
		{
			pos = y*gui->width + x;
			layer = gui->vsb_layer[gui->map[pos]];
			if (layer == NULL)
			{
				continue;
			}
			color = ((uint32_t *)layer->buffer)[((y - layer->y)*layer->width + (x - layer->x))*layer->bpp/4];
			gui->frame[pos*gui->bpp/4] = color;
		}
	}
	copy_area_to_vram(l, t, r - l, b - t, (uint8_t *)gui->frame);
}

/**
 * @brief 初始化桌面
 * 
 */
void init_desktop(struct gui_s *gui)
{
	gui->bg = create_layer(0, 0, VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel/8);
	g_layer_fill_rect(gui->bg, 0, 0, gui->bg->width, gui->bg->height, GUI_BG_COLOR);
	show_layer(gui, gui->bg);
	gui->taskbar = create_layer(0, gui->height - 40, gui->width, 40, gui->bpp);
	g_layer_fill_rect(gui->taskbar, 0, 0, gui->width, 40, 0x48cae4);
	show_layer(gui, gui->taskbar);
}