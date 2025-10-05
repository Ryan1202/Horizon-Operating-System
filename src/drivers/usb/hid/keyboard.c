#include <driver/input/input_dm.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/usb/core/descriptors.h>
#include <drivers/usb/core/usb.h>
#include <drivers/usb/hid.h>
#include <drivers/usb/keyboard.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/dynamic_device_manager.h>
#include <kernel/initcall.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <stdint.h>
#include <string.h>

DeviceDriver usb_hid_keyboard_device_driver;

DriverResult usb_hid_keyboard_init(void *_device);
DriverResult usb_hid_keyboard_start(void *_device);

DeviceOps usb_hid_keyboard_device_ops = {
	.init	 = usb_hid_keyboard_init,
	.start	 = usb_hid_keyboard_start,
	.destroy = NULL,
	.stop	 = NULL,
};

DeviceDriver usb_hid_keyboard_device_driver;
InputDevice	 usb_hid_keyboard_input_device = {
	 .type = INPUT_TYPE_KEYBOARD,
};

void usb_hid_keyboard_handler(UsbRequestBlock *urb) {
	UsbHidKeyboardReport *report	 = (UsbHidKeyboardReport *)urb->buffer;
	UsbHidKeyboard		 *keyboard	 = urb->context;
	UsbDevice			 *usb_device = keyboard->usb_device;
	if (urb->status == USB_STATUS_ACK) {
		printk(
			"Keyboard Report: Keycode: M:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
			"0x%02x "
			"0x%02x\n",
			report->modifier_keys, report->keycodes[0], report->keycodes[1],
			report->keycodes[2], report->keycodes[3], report->keycodes[4],
			report->keycodes[5]);
		if (report->modifier_keys != keyboard->last_keys[0]) {
			for (int i = 0; i < 8; i++) {
				uint8_t mask = 1 << i;
				if ((report->modifier_keys & mask) |
					(keyboard->last_keys[0] & mask)) {
					KeyEvent *event = new_key_event();
					event->page		= 0;
					event->keycode	= INPUT_KEY_EVENT_MODIFIER_BASE + i;
					if (keyboard->last_keys[0] & mask) {
						event->pressed = 0;
					} else {
						event->pressed = 1;
					}
				}
			}
		}
		if (memcmp(&keyboard->last_keys[1], report->keycodes, 6) != 0) {
			// 有按键变化
			for (int i = 0; i < 6; i++) {
				if (report->keycodes[i] == keyboard->last_keys[i + 1]) {
					continue;
				}

				bool found1 = false, found2 = false;
				for (int j = i; j < 6; j++) {
					if (report->keycodes[i] == keyboard->last_keys[j + 1]) {
						found1 = true;
					}
					if (keyboard->last_keys[i + 1] == report->keycodes[j]) {
						found2 = true;
					}
					if (found1 && found2) break;
				}
				if (!found1) {
					KeyEvent *event = new_key_event();
					event->page		= 0;
					event->keycode =
						INPUT_KEY_EVENT_KEYBOARD_BASE + report->keycodes[i];
					event->pressed = 1;
				}
				if (!found2) {
					KeyEvent *event = new_key_event();
					event->page		= 0;
					event->keycode	= INPUT_KEY_EVENT_KEYBOARD_BASE +
									 keyboard->last_keys[i + 1];
					event->pressed = 0;
				}
			}
		}
		memcpy(&keyboard->last_keys[1], report->keycodes, 6);
		keyboard->last_keys[0] = report->modifier_keys;
		urb->ep->data_toggle ^= 1;
		usb_device->hcd->ops->interrupt_transfer(usb_device->hcd, urb->ep);
	} else {
		printk("Keyboard URB Error: %d\n", urb->status);
	}
}

DriverResult usb_hid_keyboard_init(void *_device) {
	LogicalDevice				  *device	 = _device;
	UsbHidKeyboard				  *keyboard	 = device->private_data;
	struct UsbInterfaceDescriptor *interface = keyboard->interface->desc;

	UsbEndpoint *ep;
	for (int i = 0; i < interface->bNumEndpoints; i++) {
		ep = keyboard->interface->endpoints[i];
		if ((ep->desc->bmAttributes & 0x03) == USB_EP_INTERRUPT &&
			(ep->desc->bEndpointAddress >> 7) == USB_EP_IN) {
			// 找到中断输入端点
			printk(
				"Keyboard Interrupt IN Endpoint Found: 0x%02x\n",
				ep->desc->bEndpointAddress);

			int size		 = ep->desc->wMaxPacketSize & 0x7ff;
			keyboard->buffer = kmalloc(size);
			keyboard->urb	 = usb_create_urb(
				   ep, keyboard, keyboard->buffer, size, usb_hid_keyboard_handler);
			break;
		}
	}
	return DRIVER_OK;
}

DriverResult usb_hid_keyboard_start(void *_device) {
	LogicalDevice  *device	 = _device;
	UsbHidKeyboard *keyboard = device->private_data;
	UsbEndpoint	   *ep		 = keyboard->urb->ep;
	ep->data_toggle			 = 1;
	keyboard->usb_device->hcd->ops->add_interrupt_transfer(
		keyboard->usb_device->hcd, keyboard->usb_device, ep, keyboard->urb);
	return DRIVER_OK;
}

DriverResult usb_hid_keyboard_probe(
	UsbDevice *usb_device, UsbInterface *interface) {
	PhysicalDevice *physical_device = usb_device->device;

	interface->usb_driver = &usb_hid_usb_driver;

	InputDevice *input_device;
	DRIVER_RESULT_PASS(create_input_device(
		&input_device, INPUT_TYPE_KEYBOARD, &usb_hid_keyboard_device_ops,
		physical_device, &usb_hid_keyboard_device_driver));

	UsbHidKeyboard *keyboard		   = kmalloc(sizeof(UsbHidKeyboard));
	keyboard->device				   = input_device;
	keyboard->usb_device			   = usb_device;
	keyboard->interface				   = interface;
	input_device->device->private_data = keyboard;

	usb_device->state = USB_STATE_ACTIVE;

	return DRIVER_OK;
}

static __init void usb_hid_keyboard_initcall() {
	register_device_driver(&usb_hid_driver, &usb_hid_keyboard_device_driver);
}

driver_initcall(usb_hid_keyboard_initcall);
