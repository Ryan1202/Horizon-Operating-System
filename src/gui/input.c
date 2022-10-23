/**
 * @file input.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 输入设备管理
 * @version 0.1
 * @date 2022-08-20
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <gui/layer.h>
#include <gui/cursor.h>
#include <gui/input.h>
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
	int h;
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
			dx1 += tmpx - gui->cursor->rect.l;
			dy1 += tmpy - gui->cursor->rect.t;
			if (gui->move_win.move == 1)
			{
				gui->update_area.newRect.l = tmpx;
				gui->update_area.newRect.r = tmpx + dx0;
				gui->update_area.newRect.t = tmpy;
				gui->update_area.newRect.b = tmpy - dy0;
				gui->move_win.dx += tmpx - gui->cursor->rect.l;
				gui->move_win.dy += tmpy - gui->cursor->rect.t;
			}
		}
		// 鼠标左键单击
		if (mouse->lbtn != 0)
		{
			if (gui->move_win.move == 0)
			{
				for (h = gui->top - 1; h > 1; h--)
				{
					layer = gui->vsb_layer[gui->z[h]];
					if (layer->rect.l <= gui->cursor->rect.l
						&& gui->cursor->rect.l <= layer->rect.r
						&& layer->rect.t <= gui->cursor->rect.t
						&& gui->cursor->rect.b <= layer->rect.b)
					{
						gui->move_win.layer = layer;
						gui->move_win.move = 1;
						gui->update_area.oldRect.l = layer->rect.l;
						gui->update_area.oldRect.r = layer->rect.r;
						gui->update_area.oldRect.t = layer->rect.t;
						gui->update_area.newRect.b = layer->rect.b;
						break;
					}
				}
			}
		}
		else if (gui->move_win.move == 1)
		{
			gui->move_win.move = 0;
			move_layer(gui, gui->move_win.layer, gui->move_win.layer->rect.l + dx0, gui->move_win.layer->rect.t - dy0);
		}
	}
	if (dx1 != 0 || dy1 != 0)
	{
		move_layer(gui, gui->cursor, gui->cursor->rect.l+dx1, gui->cursor->rect.t + dy1);
	}
}