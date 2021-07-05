#include <device/keyboard.h>
#include <device/8042.h>
#include <device/apic.h>
#include <kernel/descriptor.h>
#include <kernel/func.h>
#include <stdint.h>
#include <config.h>

struct fifo keyfifo;
struct keystatus keyboard;
int dataoff = 0;			//最终存入FIFO的数据要加上dataoff

void init_keyboard(int off)
{
	int keybuf[512];
	dataoff = off;
	fifo_init(&keyfifo, 512, keybuf);
	i8042_send_cmd(I8042_CONFIG_WRITE);
	i8042_wait_ctr_send_ready();
	io_out8(I8042_PORT_DATA, 0x47);
	
	keyboard.left_ctrl = 0;
	keyboard.left_shift = 0;
	keyboard.left_alt = 0;
	keyboard.right_ctrl = 0;
	keyboard.right_shift = 0;
	keyboard.right_alt = 0;
	
	keyboard.num_lock = 1;
	keyboard.caps_lock = 0;
	keyboard.scroll_lock = 0;
	
	keyboard_setleds();
	
	put_irq_handler(KEYBOARD_IRQ, keyboard_handler);
	irq_enable(KEYBOARD_IRQ);
	return;
}

void keyboard_handler(int irq)
{
	uint8_t data = i8042_read_data();
	fifo_put(&keyfifo, dataoff + data);
}

char scancode_analysis(int keycode)
{
	char data = '\0';
	if (keycode <=0x35 && keycode != 0x2a && keycode != 0x1d)
	{
		if (keyboard.left_shift == 0 && keyboard.right_shift == 0)
		{
			data = scan_codes1[keycode];
		}
		else
		{
			data = scan_codes1_shift[keycode];
		}
	}
	if (keycode == 0x39)
	{
		data = ' ';
	}
	else if (keycode == 0x1d)
	{
		keyboard.left_ctrl = 1 - keyboard.left_ctrl;
	}
	else if (keycode == 0x2a)
	{
		keyboard.left_shift = 1 - keyboard.left_shift;
	}
	else if (keycode == 0x36)
	{
		keyboard.right_shift = 1 - keyboard.right_shift;
	}
	else if (keycode == 0x38)
	{
		keyboard.left_alt = 1 - keyboard.left_alt;
	}
	else if (keycode == 0x3a)
	{
		keyboard.caps_lock = 1 - keyboard.caps_lock;
		keyboard_setleds();
	}
	else if (keycode == 0x45)
	{
		keyboard.num_lock = 1 - keyboard.num_lock;
		keyboard_setleds();
	}
	else if (keycode == 0x46)
	{
		keyboard.scroll_lock = 1 - keyboard.scroll_lock;
		keyboard_setleds();
	}
	
	if (data == 0)
	{
		/* code */
	}
	return data;
}

void keyboard_setleds(void)
{
	uint8_t kb_read;
	
	i8042_wait_ctr_send_ready();
	i8042_write_data(0xed);
	do {
		kb_read = io_in8(I8042_PORT_DATA);
	} while ((kb_read =! 0xfa));
	i8042_wait_ctr_send_ready();
	i8042_write_data(keyboard.caps_lock<<2 | keyboard.num_lock<<1 | keyboard.scroll_lock);
	do {
		kb_read = io_in8(I8042_PORT_DATA);
	} while ((kb_read =! 0xfa));
}