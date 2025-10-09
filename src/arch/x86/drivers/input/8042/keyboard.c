/**
 * @file keyboard.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PS/2键盘驱动
 * @version 0.1
 * @date 2021-06
 */
#include "driver/input/input_dm.h"
#include "kernel/device.h"
#include "kernel/driver_interface.h"
#include <driver/input/key_events.h>
#include <drivers/8042.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/fifo.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <stdint.h>
#include <string.h>

DriverResult ps2_keyboard_start(void *_device);

DeviceOps ps2_keyboard_device_ops = {
	.init	 = NULL,
	.start	 = ps2_keyboard_start,
	.stop	 = NULL,
	.destroy = NULL,
};

#define SCANCODE_1_MAX 0xd8
#define SCANCODE_2_MAX 0x83

// clang-format off

static uint8_t scancodes1_usage_id[108] =
{
	0,
	KEY_ESC,		KEY_1,			KEY_2,	KEY_3,	KEY_4,	KEY_5,	KEY_6,	KEY_7,	KEY_8,	KEY_9,	KEY_0,	KEY_MINUS,	KEY_EQUAL,	KEY_BACKSPACE,
	KEY_TAB,		KEY_Q,			KEY_W,	KEY_E,	KEY_R,	KEY_T,	KEY_Y,	KEY_U,	KEY_I,	KEY_O,	KEY_P,	KEY_LEFTBRACE,	KEY_RIGHTBRACE,	KEY_ENTER,
	KEY_LEFTCTRL,	KEY_A,			KEY_S,	KEY_D,	KEY_F,	KEY_G,	KEY_H,	KEY_J,	KEY_K,	KEY_L,	KEY_SEMICOLON,	KEY_APOSTROPHE,	KEY_GRAVE,
	KEY_LEFTSHIFT,	KEY_BACKSLASH,	KEY_Z,	KEY_X,	KEY_C,	KEY_V,	KEY_B,	KEY_N,	KEY_M,	KEY_COMMA,	KEY_DOT,	KEY_SLASH,	KEY_RIGHTSHIFT,
	KEY_KEYPAD_ASTERISK,	KEY_LEFTALT,	KEY_SPACE,	KEY_CAPSLOCK,
	KEY_F1,		KEY_F2,	KEY_F3,	KEY_F4,	KEY_F5,	KEY_F6,	KEY_F7,	KEY_F8,	KEY_F9,	KEY_F10, KEY_NUMLOCK,	KEY_SCROLLLOCK,
	KEY_KEYPAD_7,	KEY_KEYPAD_8,	KEY_KEYPAD_9,	KEY_KEYPAD_MINUS,
	KEY_KEYPAD_4,	KEY_KEYPAD_5,	KEY_KEYPAD_6,	KEY_KEYPAD_PLUS,
	KEY_KEYPAD_1,	KEY_KEYPAD_2,	KEY_KEYPAD_3,
						KEY_KEYPAD_0,	KEY_KEYPAD_DOT, 0, 0, 0,
	KEY_F11,		KEY_F12, 0, 0, 0, 0
};

// 从0xE0,0x10开始, 使用最高8位区分Page(0: Keyboard/Keypad Page, 1: Consumer Page, 2: General Desktop Page)
static uint16_t scancodes1_ext_usage_id[0x5e] = {
	[0x00] = 0x1000 | KEY_CP_PREVIOUS_TRACK,[0x09] = 0x1000 | KEY_CP_NEXT_TRACK,
	[0x12] = 0x1000 | KEY_CP_PLAY_PAUSE,	[0x14] = 0x1000 | KEY_CP_STOP_EJECT,

	[0x0c] = KEY_KEYPAD_ENTER,				[0x25] = KEY_KEYPAD_SLASH,
	[0x11] = 0x1000 | KEY_CP_AL_CALC,		[0x22] = 0x1000 | KEY_CP_AL_INTERNET_BROWSER,

	[0x10] = KEY_CP_MUTE,	[0x1e] = KEY_CP_VOLUME_DECREMENT,	[0x20] = KEY_CP_VOLUME_INCREMENT,

	[0x42] = KEY_INSERT,	[0x37] = KEY_HOME,	[0x39] = KEY_PAGEUP,
	[0x43] = KEY_DELETE,	[0x3f] = KEY_END,	[0x41] = KEY_PAGEDOWN,
					
							[0x38] = KEY_UP,
	[0x3b] = KEY_LEFT,		[0x3d] = KEY_RIGHT,	[0x40] = KEY_DOWN,
	
	
	[0x0d] = KEY_RIGHTCTRL,						[0x28] = KEY_RIGHTALT,
	[0x4b] = KEY_LEFTGUI,						[0x4c] = KEY_RIGHTGUI,
	[0x4d] = KEY_APPLICATION,
	[0x4e] = 0x2000 | KEY_GDP_SYSTEM_POWER_DOWN,
	[0x4f] = 0x2000 | KEY_GDP_SYSTEM_SLEEP,		[0x53] = 0x2000 | KEY_GDP_SYSTEM_WAKEUP,
	[0x55] = 0x1000 | KEY_CP_AL_RESEARCH_SEARCH_BROWSER,
	[0x56] = 0x1000 | KEY_CP_AC_BOOKMARKS,
	[0x57] = 0x1000 | KEY_CP_AC_REFRESH,		[0x58] = 0x1000 | KEY_CP_AC_STOP,
	[0x59] = 0x1000 | KEY_CP_AC_FORWARD,		[0x5a] = 0x1000 | KEY_CP_AC_BACK,
	[0x5b] = 0x1000 | KEY_CP_AL_FILE_BROWSER,	[0x5c] = 0x1000 | KEY_CP_AL_EMAIL_READER,
	[0x5d] = 0x1000 | KEY_CP_AL_CONSUMER_CONTROL_CONFIGURATION,
};

// printscreen insert
static uint8_t scancodes2_usage_id[0x84] =
{
	0,				KEY_F9,			0,
	KEY_F5,			KEY_F3,			KEY_F1,		KEY_F2,		KEY_F12,	0,
	KEY_F10,		KEY_F8,	KEY_F6,	KEY_F4,	KEY_TAB,	KEY_GRAVE,	0,	0,
	KEY_LEFTALT,	KEY_LEFTSHIFT,	0,
	KEY_LEFTCTRL,	KEY_Q,			KEY_1,		0,		0,		0,
	KEY_Z,			KEY_S,			KEY_A,			KEY_W,		KEY_2,			0,			0,
	KEY_C,			KEY_X,			KEY_D,			KEY_E,			KEY_4,	KEY_3,		0,	0,
	KEY_SPACE,		KEY_V,			KEY_F,			KEY_T,			KEY_R,	KEY_5,		0,	0,
	KEY_N,			KEY_B,			KEY_H,			KEY_G,			KEY_Y,	KEY_6,		0,	0,	0,
	KEY_M,			KEY_J,			KEY_U,			KEY_7,			KEY_8,	0,			0,
	KEY_COMMA,		KEY_K,			KEY_I,			KEY_O,			KEY_0,	KEY_9,		0,	0,
	KEY_DOT,		KEY_SLASH,		KEY_L,			KEY_SEMICOLON,	KEY_P,	KEY_MINUS,	0,	0,	0,

	KEY_APOSTROPHE,	0,
	KEY_LEFTBRACE,	KEY_EQUAL,		0,		0,
	KEY_CAPSLOCK,	KEY_RIGHTSHIFT,	KEY_ENTER,	KEY_RIGHTBRACE,	0,
	KEY_BACKSLASH,	0,	0,	0,	0,	0,	0,	0,	0,
	KEY_BACKSPACE,	0,	0,
	KEY_KEYPAD_1,	0,
	KEY_KEYPAD_4,	KEY_KEYPAD_7, 0, 0, 0,
	KEY_KEYPAD_0,	KEY_KEYPAD_DOT,
	KEY_KEYPAD_2,	KEY_KEYPAD_5,		KEY_KEYPAD_6,			KEY_KEYPAD_8,
	KEY_ESC,		KEY_NUMLOCK,		KEY_F11,				KEY_KEYPAD_PLUS,
	KEY_KEYPAD_3,	KEY_KEYPAD_MINUS,	KEY_KEYPAD_ASTERISK,	KEY_KEYPAD_9,	KEY_SCROLLLOCK,	0,0,0,0,
	KEY_F7,
};

static uint16_t scancodes2_ext_usage_id[0x6e] = {
	[0x05] = 0x1000 | KEY_CP_PREVIOUS_TRACK,	[0x3d] = 0x1000 | KEY_CP_NEXT_TRACK,

	[0x2a] = 0x1000 | KEY_CP_AC_HOME,
	[0x08] = 0x1000 | KEY_CP_AC_BOOKMARKS,
	[0x10] = 0x1000 | KEY_CP_AC_REFRESH,
	[0x28] = 0x1000 | KEY_CP_AC_BACK,			[0x20] = 0x1000 | KEY_CP_AC_FORWARD,

	[0x0f] = KEY_LEFTGUI,	[0x17] = KEY_RIGHTGUI,
	[0x01] = KEY_RIGHTALT,	[0x04] = KEY_RIGHTCTRL,
	[0x4a] = KEY_ENTER,		[0x3a] = KEY_KEYPAD_SLASH,

	[0x11] = 0x1000 | KEY_CP_VOLUME_DECREMENT,
	[0x13] = 0x1000 | KEY_CP_MUTE,
	
	[0x22] = 0x1000 | KEY_CP_VOLUME_INCREMENT,
	[0x24] = 0x1000 | KEY_CP_PLAY_PAUSE,
	[0x2b] = 0x1000 | KEY_CP_STOP,

	[0x00] = 0x1000 | KEY_CP_AL_RESEARCH_SEARCH_BROWSER,
	[0x30] = 0x1000 | KEY_CP_AL_FILE_BROWSER,	[0x38] = 0x1000 | KEY_CP_AL_EMAIL_READER,
	[0x40] = 0x1000 | KEY_CP_AL_CONSUMER_CONTROL_CONFIGURATION,

	[0x27] = 0x2000 | KEY_GDP_SYSTEM_POWER_DOWN,
	[0x2f] = 0x2000 | KEY_GDP_SYSTEM_SLEEP,		[0x4e] = 0x2000 | KEY_GDP_SYSTEM_WAKEUP,
	
	
	[0x60] = KEY_INSERT,	[0x5c] = KEY_HOME,	[0x6d] = KEY_PAGEUP,
	[0x61] = KEY_DELETE,	[0x59] = KEY_END,	[0x6a] = KEY_PAGEDOWN,
							[0x65] = KEY_UP,
	[0x5b] = KEY_LEFT,		[0x62] = KEY_DOWN,	[0x64] = KEY_RIGHT,
	
};

// clang-format on

#define KEYBOARD_CAPSLOCK	0x04
#define KEYBOARD_NUMLOCK	0x02
#define KEYBOARD_SCROLLLOCK 0x01
typedef struct {
	int		port;
	uint8_t scancode_num;

	uint8_t	 *scancode;
	uint8_t	  scancode_max;
	uint16_t *scancode_ext;

	uint8_t extended_mode; // 0: none, 1: 0xe0, 2: 0xe1
	uint8_t release;	   // scancode 2专用，0: none, 1: 0xf0

	uint8_t locks;
} Ps2Keyboard;

void scancode_handle(Ps2Keyboard *kbd, uint8_t keycode);
void keyboard_setleds(Ps2Keyboard *kbd);

void keyboard_irq_handler(void *arg) {
	Ps2Keyboard *kbd = (Ps2Keyboard *)arg;
	uint16_t	 data;
	data = i8042_read_data();
	scancode_handle(kbd, data);
}

uint16_t scancode_handle_ext_1(Ps2Keyboard *kbd, uint8_t keycode) {
	uint16_t data = kbd->scancode_ext[(keycode & 0x7f) - 0x10];
	if (kbd->scancode_num == 1) {
		// print screen pressed: 0xE0 0x2A 0xE0 0x37
		// print screen released: 0xE0 0xB7 0xE0 0xAA

		// 0xE0字节会被scancode_handle设备为扩展码自动跳过，通过第二个字节判断是pressed还是released，
		// 第四个字节判断是否为print screen
		if (keycode == 0x2a) {
			kbd->release = false;
			return 0;
		} else if (keycode == 0xb7) {
			kbd->release = true;
			return 0;
		}
		if (keycode == 0x37 || keycode == 0xaa) { return KEY_PRINTSCREEN; }
	} else if (kbd->scancode_num == 2) {
		// print screen pressed: 0xE0 0x12 0xE0 0x7C
		// print screen released: 0xE0 0xF0 0x7C 0xE0 0xF0 0x12

		// 由于需要判断结束字节，需要结合release状态(是否遇到过0xF0)判断
		if (kbd->release == false && keycode == 0x7C) {
			return KEY_PRINTSCREEN;
		}
		if (kbd->release == true && keycode == 0x12) { return KEY_PRINTSCREEN; }
	}
	return data;
}

uint16_t scancode_handle_ext_2(Ps2Keyboard *kbd, uint8_t keycode) {
	// pause只有pressed
	if (kbd->scancode_num == 1) {
		// pause pressed: 0xe1 0x1d 0x45 0xe1 0x9d 0xc5
		// scancode 1
		// 0xe1开头的扩展按键只有pause，最后一个字节没有冲突直接通过最后一个字节判断
		if (keycode == 0xc5) {
			kbd->release = false;
			return KEY_PAUSE;
		}
	} else if (kbd->scancode_num == 2) {
		// pause pressed: 0xe1 0x14 0x77 0xe1 0xf0 0x14 0xf0 0x77

		// 0xf0字节会被scancode_handle设备为释放码，所以需要通过最后一个字节判断
		// 又由于序列中包含两个0x77，而前一个0x77前面没有0xf0，所以要结合release状态(是否遇到过0xf0)判断
		if (keycode == 0x77 && kbd->release == true) {
			kbd->release = false;
			return KEY_PAUSE;
		}
	}
	return 0;
}

void scancode_handle(Ps2Keyboard *kbd, uint8_t keycode) {
	bool	 press = true;
	uint16_t data  = kbd->scancode[keycode];
	if (keycode == 0xe0) {
		kbd->extended_mode = 1;
		return;
	} else if (keycode == 0xe1) {
		kbd->extended_mode = 2;
		return;
	} else if (kbd->scancode_num == 2 && keycode == 0xf0) {
		kbd->release = 1;
		return;
	} else if (keycode > kbd->scancode_max) {
		return;
	} else {
		if (kbd->scancode_num == 1 && data & 0x80) press = false;
	}

	if (kbd->extended_mode == 1) {
		data = scancode_handle_ext_1(kbd, keycode);
		if (data == 0) return; // 还没处理完
	} else if (kbd->extended_mode == 2) {
		data = scancode_handle_ext_2(kbd, keycode);
		if (data == 0) return; // 还没处理完
	}

	press			  = (kbd->release == 1) ? false : press;
	uint8_t old_locks = kbd->locks;
	if (data == KEY_CAPSLOCK) { kbd->locks ^= KEYBOARD_CAPSLOCK; }
	if (data == KEY_NUMLOCK) { kbd->locks ^= KEYBOARD_NUMLOCK; }
	if (data == KEY_SCROLLLOCK) { kbd->locks ^= KEYBOARD_SCROLLLOCK; }
	if (old_locks != kbd->locks) { keyboard_setleds(kbd); }
	if (data != 0) {
		uint8_t tmp = data >> 12;
		if (tmp == 0) {
			new_key_event(
				INPUT_KEY_EVENT_KEYBOARD_BASE + data & 0xfff, press,
				INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
		} else if (tmp == 1) {
			new_key_event(data & 0xfff, press, INPUT_KEY_PAGE_CONSUMER);
		} else if (tmp == 2) {
			new_key_event(data & 0xfff, press, INPUT_KEY_PAGE_GENERAL_DESKTOP);
		}
	}
	if (kbd->extended_mode) kbd->extended_mode = 0;
	if (kbd->release) kbd->release = 0;
}

void keyboard_setleds(Ps2Keyboard *kbd) {
	i8042_wait_ctr_send_ready();
	i8042_write_data(I8042_KBD_CMD_SET_LEDS);
	i8042_wait_ctr_send_ready();
	io_in_byte(I8042_PORT_DATA);
	i8042_wait_ctr_send_ready();
	i8042_write_data(kbd->locks & 0x07);
	i8042_wait_ctr_send_ready();
	io_in_byte(I8042_PORT_DATA);
}

DriverResult ps2_keyboard_start(void *_device) {
	LogicalDevice *device = _device;
	Ps2Keyboard	  *kbd	  = device->private_data;

	i8042_disable_interrupt(kbd->port);
	// 获取scancode集
	i8042_wait_ctr_send_ready();
	i8042_write_data(I8042_KBD_CMD_GET_SET_SCANCODE_SET);
	i8042_wait_ctr_send_ready();
	if (i8042_read_data() != 0xfa) {
		printk(
			"[i8042]PS/2 Port%d Device get scancode set failed!\n",
			kbd->port + 1);
		return DRIVER_ERROR_OTHER;
	}
	i8042_wait_ctr_send_ready();
	i8042_write_data(0);
	i8042_wait_ctr_send_ready();
	if (i8042_read_data() != 0xfa) {
		printk(
			"[i8042]PS/2 Port%d Device get scancode set failed!\n",
			kbd->port + 1);
		return DRIVER_ERROR_OTHER;
	}
	int num = i8042_read_data();
	if (num == 0x43) {
		kbd->scancode	  = scancodes1_usage_id;
		kbd->scancode_max = SCANCODE_1_MAX;
		kbd->scancode_ext = scancodes1_ext_usage_id;
		kbd->scancode_num = 1;
	} else if (num == 0x41) {
		kbd->scancode	  = scancodes2_usage_id;
		kbd->scancode_max = SCANCODE_2_MAX;
		kbd->scancode_ext = scancodes2_ext_usage_id;
		kbd->scancode_num = 2;
	} else {
		i8042_wait_ctr_send_ready();
		i8042_write_data(I8042_KBD_CMD_GET_SET_SCANCODE_SET);
		i8042_wait_ctr_send_ready();
		if (i8042_read_data() != 0xfa) {
			printk(
				"[i8042]PS/2 Port%d Device set scancode set failed!\n",
				kbd->port + 1);
			return DRIVER_ERROR_OTHER;
		}
		i8042_wait_ctr_send_ready();
		i8042_write_data(1);
		i8042_wait_ctr_send_ready();
		if (i8042_read_data() != 0xfa) {
			printk(
				"[i8042]PS/2 Port%d Device set scancode set failed!\n",
				kbd->port + 1);
			return DRIVER_ERROR_OTHER;
		}
		kbd->scancode	  = scancodes1_usage_id;
		kbd->scancode_max = SCANCODE_1_MAX;
		kbd->scancode_ext = scancodes1_ext_usage_id;
		kbd->scancode_num = 1;
	}
	kbd->release	   = 0;
	kbd->extended_mode = 0;
	// 关闭编码转换
	i8042_wait_ctr_send_ready();
	i8042_send_cmd(I8042_CMD_READ);
	i8042_wait_ctr_send_ready();
	uint8_t cfg = i8042_read_data();
	cfg &= ~I8042_CFG_TRANS1;
	i8042_send_cmd(I8042_CMD_WRITE);
	i8042_wait_ctr_send_ready();
	i8042_write_data(cfg);

	I8042Device *i8042			   = i8042_device->private_data;
	// 手动修改irq的处理函数和参数
	i8042->irq[kbd->port]->handler = keyboard_irq_handler;
	i8042->irq[kbd->port]->arg	   = kbd;

	i8042_wait_ctr_send_ready();
	i8042_write_data(I8042_KBD_CMD_ENABLE_SCANNING);
	i8042_read_data();

	i8042_enable_interrupt(kbd->port);

	return DRIVER_OK;
}

void ps2_keyboard_register(PhysicalDevice *physical_device, int port) {
	InputDevice *in;
	create_input_device(
		&in, INPUT_TYPE_KEYBOARD, &ps2_keyboard_device_ops, physical_device,
		&i8042_device_driver);
	Ps2Keyboard *kbd		 = kmalloc(sizeof(Ps2Keyboard));
	kbd->port				 = port;
	in->device->private_data = kbd;
	kbd->locks				 = 0;
}