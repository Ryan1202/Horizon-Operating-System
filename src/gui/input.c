/**
 * @file input.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 输入设备管理
 * @version 0.2
 * @date 2022-11-12
 * 
 * @copyright Copyright (c) Ryan Wang 2022
 * 
 */
#include <gui/layer.h>
#include <gui/cursor.h>
#include <gui/input.h>
#include <gui/window.h>
#include <kernel/memory.h>
#include <kernel/fifo.h>
#include <fs/fs.h>
#include <math.h>

void init_input(struct gui_s *gui)
{
	gui->input = kmalloc(sizeof(struct input_devices));
	struct mouse *mouse = &gui->input->mouse;
	struct keyboard *keyboard = &gui->input->keyboard;
	
	mouse->lbtn = mouse->mbtn = mouse->rbtn = 0;
	mouse->status = CSTAT_DEFAULT;
	set_cursor_mode(gui->cursor, mouse->status);
	mouse->wait_ack = 1;
	mouse->inode = fs_open("/dev/mouse");
	mouse->inode->f_ops.ioctl(mouse->inode, 1, (uint32_t)&mouse->fifo);
	
	keyboard->lctrl = keyboard->rctrl = 0;
	keyboard->lalt = keyboard->ralt = 0;
	keyboard->lshift = keyboard->rshift = 0;
	keyboard->num_lock = keyboard->caps_lock = keyboard->scroll_lock = 0;
}

void input_handler(struct gui_s *gui)
{
	int data, dx0, dy0, dx1 = 0, dy1 = 0, tmpx, tmpy;
	struct mouse *mouse = &gui->input->mouse;
	struct keyboard *keyboard = &gui->input->keyboard;
	struct layer_s *layer;
	int h, x, y;
	while (fifo_status(mouse->fifo) != 0)
	{
		dx0 = dy0 = 0;
		data = fifo_get(mouse->fifo);
		if (mouse->wait_ack && data == 0xfa)
		{
			mouse->wait_ack = 0;
		}
		else if ((data & 0xc8) == 0x08)
		{
			mouse->mbtn = (data & 0x04) >> 2;
			mouse->rbtn = (data & 0x02) >> 1;
			mouse->lbtn = (data & 0x01);
			dx0 = fifo_get(mouse->fifo);
			dy0 = fifo_get(mouse->fifo);
			if (data & 0x10)
			{
				dx0 |= 0xffffff00;
			}
			if (data & 0x20)
			{
				dy0 |= 0xffffff00;
			}
			tmpx = MIN(MAX(gui->cursor->rect.l + dx0, 0), gui->width-1);
			tmpy = MAX(MIN(gui->cursor->rect.t - dy0, gui->height), 0);
			dx1 = tmpx - gui->cursor->rect.l;
			dy1 = tmpy - gui->cursor->rect.t;
			move_layer(gui, gui->cursor, gui->cursor->rect.l+dx1, gui->cursor->rect.t + dy1);
		}
		for (h = gui->top - 1; h > 1; h--)
		{
			layer = gui->vsb_layer[gui->z[h]];
			if (layer->win == NULL)
			{
				continue;
			}
			x = gui->cursor->rect.l - layer->rect.l;
			y = gui->cursor->rect.t - layer->rect.t;
			if (layer->rect.l - 4 <= gui->cursor->rect.l && gui->cursor->rect.l < layer->rect.r + 4 &&
				layer->rect.t - 4 <= gui->cursor->rect.t && gui->cursor->rect.t < layer->rect.b + 4)
			{
				if ((((uint32_t *)layer->buffer)[y * (layer->rect.r - layer->rect.l) + x] & 0xff000000) != 0xff000000)
				{
					if (layer != gui->focus)
					{
						gui->focus = layer;
						layer_set_z(gui, layer, gui->top - 1);
					}
					layer->win->win_handler(layer->win, WIN_MSG_MOVE,
						gui->cursor->rect.l, gui->cursor->rect.t);
					if (mouse->lbtn != 0)
					{
						layer->win->win_handler(layer->win, WIN_MSG_LCLICK,
							gui->cursor->rect.l, gui->cursor->rect.t);
						if (gui->move_win.move == 0 && layer != NULL)
						{
							gui->move_win.layer = layer;
							gui->move_win.dx = gui->cursor->rect.l - layer->rect.l;
							gui->move_win.dy = gui->cursor->rect.t - layer->rect.t;
							gui->move_win.move = 1;
						}
					}
					if (mouse->mbtn != 0)
					{
						layer->win->win_handler(layer->win, WIN_MSG_MCLICK,
							gui->cursor->rect.l, gui->cursor->rect.t);
					}
					if (mouse->rbtn != 0)
					{
						layer->win->win_handler(layer->win, WIN_MSG_RCLICK,
							gui->cursor->rect.l, gui->cursor->rect.t);
					}
				}
			}
			else if (layer->win->triggers.hovered != NULL)
			{
				layer->win->win_handler(layer->win, WIN_MSG_MOVE,
					gui->cursor->rect.l, gui->cursor->rect.t);
			}
		}
	}
	if (gui->move_win.move == 1 && mouse->lbtn == 0)
	{
		gui->move_win.move = 0;
		move_layer(gui, gui->move_win.layer,
			gui->cursor->rect.l - gui->move_win.dx, gui->cursor->rect.t - gui->move_win.dy);
	}
}