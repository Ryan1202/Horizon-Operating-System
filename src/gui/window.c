/**
 * @file window.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 窗口管理
 * @version 0.3
 * @date 2022-11-13
 * 
 * @copyright Copyright (c) Ryan Wang 2022
 * 
 */
#include <gui/graphic.h>
#include <gui/window.h>
#include <kernel/memory.h>

const char close_btn[10][10] = {
	".o.......o",
	"..o.....o.",
	"...o...o..",
	"....o.o...",
	".....o....",
	"....o.o...",
	"...o...o..",
	"..o.....o.",
	".o.......o"
};

void draw_win(struct win_s *win)
{
	int width = win->layer->rect.r - win->layer->rect.l;
	int height = win->layer->rect.b - win->layer->rect.t;
	g_fill_rect(win->layer, 0, 0, width, height, WIN_BACK_COLOR);
	g_fill_rect(win->layer, 0, 0, width, 30, WIN_TITLE_COLOR);
	g_print_string(win->layer, width/2 - win->title.length*9/2, 7, font16, win->title.text, WIN_FRONT_COLOR);
	
	int i, j;
	for(i = 0; i < 10; i++)
	{
		for(j = 0; j < 10; j++)
		{
			if(close_btn[i][j] == 'o')
			{
				((uint32_t *)win->layer->buffer)[((i+10)*width+j+width-20)*win->layer->bpp/4] = WIN_FRONT_COLOR;
			}
			else if(close_btn[i][j] == '.')
			{
				((uint32_t *)win->layer->buffer)[((i+10)*width+j+width-20)*win->layer->bpp/4] = WIN_TITLE_COLOR;
			}
		}
	}
	return;
}

void close_btn_trigger(struct win_s *win, uint32_t value, uint32_t *private_data)
{
	int i, j, color1, color2;
	int width = win->layer->rect.r - win->layer->rect.l;
	struct Rect refresh_rect = {
		win->layer->rect.r - 30,
		win->layer->rect.r,
		win->layer->rect.t,
		win->layer->rect.t + 30
	};
	if ((value & (~1)) == WIN_MSG_LCLICK)
	{
		close_win(win);
		return;
	}
	if ((value & 1) == 1)
	{
		color1 = 0xe2831b;
		color2 = WIN_FRONT_COLOR;
	}
	else
	{
		color1 = WIN_FRONT_COLOR;
		color2 = WIN_TITLE_COLOR;
	}
	g_fill_rect(win->layer, width - 30, 0, width, 30, color2);
	for(i = 0; i < 10; i++)
	{
		for(j = 0; j < 10; j++)
		{
			if(close_btn[i][j] == 'o')
			{
				((uint32_t *)win->layer->buffer)[((i+10)*width+j+width-20)*win->layer->bpp/4] = color1;
			}
			else if (close_btn[i][j] == '.')
			{
				((uint32_t *)win->layer->buffer)[((i+10)*width+j+width-20)*win->layer->bpp/4] = color2;
			}
		}
	}
	gui_refresh(win->gui, &refresh_rect);
}

struct win_s *create_win(struct gui_s *gui, int x, int y, int width, int height, int bpp, char *title)
{
	struct win_s *win = kmalloc(sizeof(struct win_s));
	
	win->layer = create_layer(x, y, width, height, bpp);
	string_init(&win->title);
	string_new(&win->title, title, strlen(title));
	win->win_handler = &default_win_handler;
	win->layer->win = win;
	win->gui = gui;
	
	list_init(&win->triggers.list_head);
	win->triggers.hovered = NULL;
	struct trigger_item_s *item = register_trigger(WIN_MSG_MOVE, width - 32, 2, width, 30, NULL);
	item->func = &close_btn_trigger;
	list_add(&item->list, &win->triggers.list_head);
	item = register_trigger(WIN_MSG_LCLICK, width - 32, 2, width, 30, NULL);
	item->func = &close_btn_trigger;
	list_add(&item->list, &win->triggers.list_head);

	draw_win(win);
	gui->focus = win->layer;
	return win;
}

void close_win(struct win_s *win)
{
	struct trigger_item_s *cur, *next;
	if (win->gui->focus == win->layer)
	{
		win->gui->focus = win->gui->bg;
	}
	list_for_each_owner_safe(cur, next, &win->triggers.list_head, list)
	{
		kfree(cur);
	}
	string_del(&win->title);
	delete_layer(win->gui, win->layer);
	kfree(win);
}

void default_win_handler(struct win_s *win, uint32_t msgtype, uint32_t value1, uint32_t value2)
{
	int width = win->layer->rect.r - win->layer->rect.l;
	int x = value1 - win->layer->rect.l;
	int y = value2 - win->layer->rect.t;
	struct trigger_item_s *cur, *next;
	switch (msgtype)
	{
	case WIN_MSG_MOVE:
	case WIN_MSG_LCLICK:
	case WIN_MSG_RCLICK:
		list_for_each_owner_safe(cur, next, &win->triggers.list_head, list)
		{
			if (cur->type != msgtype)
			{
				continue;
			}
			if (cur->range.l <= x && x <= cur->range.r &&
				cur->range.t <= y && y <= cur->range.b)
			{
				cur->triggered = 1;
				win->triggers.hovered = cur;
				cur->func(win, msgtype | cur->triggered, cur->private_data);
				break;
			}
			else if (win->triggers.hovered == cur)
			{
				cur->triggered = 0;
				win->triggers.hovered = NULL;
				cur->func(win, msgtype | cur->triggered, cur->private_data);
			}
		}
		break;
	
	default:
		break;
	}
}