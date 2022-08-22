/**
 * @file window.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 窗口管理
 * @version 0.1
 * @date 2022-08-21
 */
#include <gui/graphic.h>
#include <gui/window.h>
#include <kernel/memory.h>

void draw_win(struct win_s *win)
{
	g_fill_rect(win->layer, 0, 0, win->layer->width, win->layer->height, 0x333333);
	g_fill_rect(win->layer, 0, 0, win->layer->width, 30, 0x101010);
	g_print_string(win->layer, win->layer->width/2 - win->title.length*9/2, 7, font16, win->title.text, 0x656565);
	return;
}

struct win_s *create_win(int x, int y, int width, int height, int bpp, char *title)
{
	struct win_s *win = kmalloc(sizeof(struct win_s));
	win->layer = create_layer(x, y, width, height, bpp);
	string_init(&win->title);
	string_new(&win->title, title, strlen(title));
	draw_win(win);
	return win;
}