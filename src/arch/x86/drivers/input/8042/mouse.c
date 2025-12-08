/**
 * @file mouse.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PS/2鼠标驱动
 * @version 0.1
 * @date 2021-06
 */
#include "driver/input/key_events.h"
#include "kernel/device.h"
#include <driver/input/input_dm.h>
#include <drivers/8042.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/fifo.h>
#include <kernel/func.h>
#include <stdint.h>

#define MOUSE_Y_OVERFLOW 0x80
#define MOUSE_X_OVERFLOW 0x40
#define MOUSE_Y_SIGN	 0x20
#define MOUSE_X_SIGN	 0x10
#define MOUSE_MIDDLE_BTN 0x04
#define MOUSE_RIGHT_BTN	 0x02
#define MOUSE_LEFT_BTN	 0x01

struct fifo mouse_fifo;

DriverResult ps2_mouse_start(void *_device);

typedef struct {
	int port;

	int state;

	int byte0, x, y;

	// 记录左中右键状态来决定是否发送事件
	int btn;
} Ps2Mouse;

DeviceOps ps2_mouse_device_ops = {
	.init	 = NULL,
	.start	 = ps2_mouse_start,
	.stop	 = NULL,
	.destroy = NULL,
};

void mouse_handler(void *arg) {
	Ps2Mouse *mouse = (Ps2Mouse *)arg;
	uint8_t	  data	= i8042_read_data();
	switch (mouse->state) {
	case 0:
		if ((data & 0x08) != 0x08) { return; }
		mouse->byte0 = data;
		mouse->state = 1;
		break;
	case 1:
		mouse->x	 = data;
		mouse->state = 2;
		break;
	case 2:
		mouse->y	 = data;
		mouse->state = 0;
		break;
	default:
		mouse->state = 0;
		return;
	}
	if (mouse->state == 1) { // 原先是0
		bool old_left  = !!(mouse->btn & MOUSE_LEFT_BTN);
		bool old_mid   = !!(mouse->btn & MOUSE_MIDDLE_BTN);
		bool old_right = !!(mouse->btn & MOUSE_RIGHT_BTN);
		bool left	   = !!(data & MOUSE_LEFT_BTN);
		bool mid	   = !!(data & MOUSE_MIDDLE_BTN);
		bool right	   = !!(data & MOUSE_RIGHT_BTN);
		if (mouse->btn !=
			(data & (MOUSE_LEFT_BTN | MOUSE_MIDDLE_BTN | MOUSE_RIGHT_BTN))) {
			// 按键状态变化
			if (left ^ old_left) {
				// 左键状态变化
				new_key_event(
					INPUT_KEY_EVENT_MOUSE_BASE + KEY_MOUSE_LEFT, left,
					INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
			}
			if (mid ^ old_mid) {
				// 中键状态变化
				new_key_event(
					INPUT_KEY_EVENT_MOUSE_BASE + KEY_MOUSE_MIDDLE, mid,
					INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
			}
			if (right ^ old_right) {
				// 右键状态变化
				new_key_event(
					INPUT_KEY_EVENT_MOUSE_BASE + KEY_MOUSE_RIGHT, right,
					INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
			}
			mouse->btn =
				data & (MOUSE_LEFT_BTN | MOUSE_MIDDLE_BTN | MOUSE_RIGHT_BTN);
		}
	}
	if (mouse->state == 0) {
		if (mouse->x != 0 || mouse->y != 0) {
			int16_t dx = (mouse->byte0 & MOUSE_X_SIGN)
						   ? (0xff00 | (uint16_t)mouse->x)
						   : (uint16_t)mouse->x;
			int16_t dy = (mouse->byte0 & MOUSE_Y_SIGN)
						   ? (0xff00 | (uint16_t)mouse->y)
						   : (uint16_t)mouse->y;
			new_pointer_event(dx, -dy, POINTER_TYPE_MOVE);
		}
		mouse->byte0 = mouse->x = mouse->y = 0;
	}
}

DriverResult ps2_mouse_start(void *_device) {
	LogicalDevice *logical_device = (LogicalDevice *)_device;
	Ps2Mouse	  *mouse		  = (Ps2Mouse *)logical_device->private_data;

	i8042_disable_interrupt(mouse->port);

	i8042_wait_ctr_send_ready();
	i8042_send_cmd(I8042_CMD_SEND_TO_P2);
	i8042_wait_ctr_send_ready();
	i8042_write_data(I8042_KBD_CMD_ENABLE_SCANNING);
	i8042_wait_ctr_send_ready();
	if (i8042_read_data() != 0xfa) {
		printk("[i8042]PS/2 Port2 Device enable failed!\n");
		return DRIVER_ERROR_OTHER;
	}

	// 手动修改irq的处理函数和参数
	I8042Device *i8042				 = i8042_device->private_data;
	i8042->irq[mouse->port]->handler = mouse_handler;
	i8042->irq[mouse->port]->arg	 = mouse;

	mouse->state = 0;
	i8042_enable_interrupt(mouse->port);

	return DRIVER_OK;
}

void ps2_mouse_register(PhysicalDevice *physical_device, int port) {
	InputDevice *in;
	create_input_device(
		&in, INPUT_TYPE_MOUSE, &ps2_mouse_device_ops, physical_device,
		&i8042_device_driver);
	Ps2Mouse *mouse			 = kzalloc(sizeof(Ps2Mouse));
	mouse->port				 = port;
	in->device->private_data = mouse;
}