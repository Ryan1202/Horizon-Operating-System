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
	int data, dx = 0, dy = 0;
	struct mouse *mouse = &gui->input->mouse;
	struct keyboard *keyboard = &gui->input->keyboard;
	struct layer_s *layer;
	int h;
	if (fifo_status(mouse->fifo) != 0)
	{
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
			dx = fifo_get(mouse->fifo);
			dy = fifo_get(mouse->fifo);
			if (data & 0x10)
			{
				dx |= 0xffffff00;
			}
			if (data & 0x20)
			{
				dy |= 0xffffff00;
			}
			move_layer(gui, gui->cursor, MAX(gui->cursor->x + dx, 0), gui->cursor->y - dy);
			if (gui->move_win.move == 1)
			{
				gui->move_win.dx += dx;
				gui->move_win.dy += dy;
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
					if (layer->x <= gui->cursor->x
						&& gui->cursor->x <= layer->x + layer->width
						&& layer->y <= gui->cursor->y
						&& gui->cursor->y <= layer->y + layer->height)
					{
						gui->move_win.layer = layer;
						gui->move_win.dx = dx;
						gui->move_win.dy = dy;
						gui->move_win.move = 1;
						break;
					}
				}
			}
		}
		else if (gui->move_win.move == 1)
		{
			gui->move_win.move = 0;
			move_layer(gui, gui->move_win.layer, gui->move_win.layer->x + dx, gui->move_win.layer->y - dy);
		}
	}
}