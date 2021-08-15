#include <drivers/8042.h>
#include <drivers/apic.h>
#include <kernel/descriptor.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/driver.h>
#include <kernel/console.h>
#include <kernel/fifo.h>
#include <stdint.h>
#include <config.h>

static char scan_codes1[95] =
{
	0,
	0,		'1',	'2',	'3',	'4',	'5',	'6',	'7',	'8',	'9',	'0',	'-',	'=',	'\b',
	0,		'q',	'w',	'e',	'r',	't',	'y',	'u',	'i',	'o',	'p',	'[',	']',	'\n',
	0,		'a',	's',	'd',	'f',	'g',	'h',	'j',	'k',	'l',	';',	'\'',	'`',
	0,		'\\',	'z',	'x',	'c',	'v',	'b',	'n',	'm',	',',	'.',	'/',	0,
};
static char scan_codes1_shift[95] =
{
	0,
	0,		'!',	'@',	'#',	'$',	'%',	'^',	'&',	'*',	'(',	')',	'_',	'+',	'\b',
	0,		'Q',	'W',	'E',	'R',	'T',	'Y',	'U',	'I',	'O',	'P',	'{',	'}',	'\n',
	0,		'A',	'S',	'D',	'F',	'G',	'H',	'J',	'K',	'L',	':',	'\"',	'~',
	0,		'|',	'Z',	'X',	'C',	'V',	'B',	'N',	'M',	'<',	'>',	'?',	0,
};

static char scan_codes2[0x5d+1] =
{
	0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		'`',	0,		0,		0,		0,
	0,		0,		'q',	'1',	0,		0,		0,		'z',	's',	'a',	'w',	'2',	0,		0,		'c',	'x',	'd',	'e',	'4',
	'3',	0,		0,		' ',	'v',	'f',	't',	'r',	'5',	0,		0,		'n',	'b',	'h',	'g',	'y',	'6',	0,		0,
	0,		'm',	'j',	'u',	'7',	'8',	0,		0,		',',	'k',	'i',	'o',	'0',	'9',	0,		0,		'.',	'/',	'l',
	';',	'p',	'-',	0,		0,		0,		'\'',	0,		'[',	'=',	0,		0,		0,		0,		0,		']',	0,		'\\'
};
static char scan_codes2_shift[0x5d+1] =
{
	0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		'~',	0,		0,		0,		0,
	0,		0,		'Q',	'!',	0,		0,		0,		'Z',	'S',	'A',	'W',	'@',	0,		0,		'C',	'X',	'D',	'E',	'$',
	'$',	0,		0,		0,		'V',	'F',	'T',	'R',	'%',	0,		0,		'N',	'B',	'H',	'G',	'Y',	'^',	0,		0,
	0,		'M',	'J',	'U',	'&',	'*',	0,		0,		'<',	'K',	'I',	'O',	'P',	'(',	0,		0,		'>',	'?',	'L',
	':',	'P',	'_',	0,		0,		0,		'\"',	0,		'{',	'+',	0,		0,		0,		0,		0,		'}',	0,		'|'
};

#define KEYBOARD_IRQ	1

#define DRV_NAME "General PS/2 Driver(Keyboard)"
#define DEV_NAME "PS/2 Keyboard"

typedef struct
{
	int num_lock, caps_lock, scroll_lock;		//键盘锁
	int left_ctrl, right_ctrl;
	int left_shift, right_shift;
	int left_alt, right_alt;
} device_extension_t;

void keyboard_handler(int irq);
static status_t keyboard_enter(driver_t *drv_obj);
static status_t keyboard_exit(driver_t *drv_obj);
char scancode_analysis(device_extension_t *devext, int keycode);
void keyboard_setleds(device_extension_t *devext);

driver_func_t keyboard_driver = {
	.driver_enter = keyboard_enter,
	.driver_exit = keyboard_exit,
	.driver_open = NULL,
	.driver_close = NULL,
	.driver_read = NULL,
	.driver_write = NULL,
	.driver_devctl = NULL
};

struct fifo keyfifo;
int keybuf[512];

static status_t keyboard_enter(driver_t *drv_obj)
{
	device_t *devobj;
	device_extension_t *devext;
	
	device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_KEYBOARD, &devobj);
	devext = devobj->device_extension;
	
	fifo_init(&keyfifo, 512, keybuf);
	i8042_send_cmd(I8042_CONFIG_WRITE);
	i8042_wait_ctr_send_ready();
	io_out8(I8042_PORT_DATA, 0x47);
	
	devext->left_ctrl = 0;
	devext->left_shift = 0;
	devext->left_alt = 0;
	devext->right_ctrl = 0;
	devext->right_shift = 0;
	devext->right_alt = 0;
	
	devext->num_lock = 1;
	devext->caps_lock = 0;
	devext->scroll_lock = 0;
	
	keyboard_setleds(devext);
	
	put_irq_handler(KEYBOARD_IRQ, keyboard_handler);
	irq_enable(KEYBOARD_IRQ);
	return SUCCUESS;
}

static status_t keyboard_exit(driver_t *drv_obj)
{
	device_t *devobj, *next;
	// device_extension_t *ext;
	list_for_each_owner_safe(devobj, next, &drv_obj->device_list, list)
	{
		
		
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

void keyboard_handler(int irq)
{
	uint8_t data = i8042_read_data();
	fifo_put(&keyfifo, data);
}

char scancode_analysis(device_extension_t *devext, int keycode)
{
	char data = '\0';
	if (keycode <=0x35 && keycode != 0x2a && keycode != 0x1d)
	{
		if (devext->left_shift == 0 && devext->right_shift == 0)
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
		devext->left_ctrl = 1 - devext->left_ctrl;
	}
	else if (keycode == 0x2a)
	{
		devext->left_shift = 1 - devext->left_shift;
	}
	else if (keycode == 0x36)
	{
		devext->right_shift = 1 - devext->right_shift;
	}
	else if (keycode == 0x38)
	{
		devext->left_alt = 1 - devext->left_alt;
	}
	else if (keycode == 0x3a)
	{
		devext->caps_lock = 1 - devext->caps_lock;
		keyboard_setleds(devext);
	}
	else if (keycode == 0x45)
	{
		devext->num_lock = 1 - devext->num_lock;
		keyboard_setleds(devext);
	}
	else if (keycode == 0x46)
	{
		devext->scroll_lock = 1 - devext->scroll_lock;
		keyboard_setleds(devext);
	}
	
	if (data == 0)
	{
		/* code */
	}
	return data;
}

void keyboard_setleds(device_extension_t *devext)
{
	uint8_t kb_read;
	
	i8042_wait_ctr_send_ready();
	i8042_write_data(0xed);
	do {
		kb_read = io_in8(I8042_PORT_DATA);
	} while ((kb_read =! 0xfa));
	i8042_wait_ctr_send_ready();
	i8042_write_data(devext->caps_lock<<2 | devext->num_lock<<1 | devext->scroll_lock);
	do {
		kb_read = io_in8(I8042_PORT_DATA);
	} while ((kb_read =! 0xfa));
}

static __init void keyboard_driver_entry(void)
{
	if (driver_create(keyboard_driver, DRV_NAME) < 0)
	{
		printk(COLOR_RED"[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(keyboard_driver_entry);