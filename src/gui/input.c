#include <gui/layer.h>
#include <gui/cursor.h>
#include <gui/input.h>
#include <kernel/memory.h>
#include <kernel/fifo.h>
#include <fs/fs.h>

void init_input(struct gui_s *gui)
{
	gui->input = kmalloc(sizeof(struct input_devices));
	struct mouse *mouse = &gui->input->mouse;
	struct keyboard *keyboard = &gui->input->keyboard;
	
	mouse->lbtn = mouse->mbtn = mouse->rbtn = 0;
	mouse->status = CSTAT_BUSY;
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
	int data, deltax, deltay;
	struct mouse *mouse = &gui->input->mouse;
	struct keyboard *keyboard = &gui->input->keyboard;
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
			deltax = fifo_get(mouse->fifo);
			deltay = fifo_get(mouse->fifo);
			if (data & 0x10)
			{
				deltax |= 0xffffff00;
			}
			if (data & 0x20)
			{
				deltay |= 0xffffff00;
			}
			move_layer(gui, gui->cursor, gui->cursor->x + deltax, gui->cursor->y - deltay);
		}
	}
}