#include "driver/usb/descriptors.h"
#include "driver/usb/usb_dm.h"
#include "kernel/dynamic_device_manager.h"
#include "kernel/list.h"
#include <driver/input/input_dm.h>
#include <driver/usb/usb.h>
#include <drivers/usb/hid.h>
#include <drivers/usb/keyboard.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <stdint.h>
#include <string.h>

DeviceDriver usb_hid_keyboard_device_driver;

DriverResult usb_hid_keyboard_init(Device *device);
DriverResult usb_hid_keyboard_start(Device *device);

DeviceDriverOps usb_hid_keyboard_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
DeviceOps usb_hid_keyboard_device_ops = {
	.init	 = usb_hid_keyboard_init,
	.start	 = usb_hid_keyboard_start,
	.destroy = NULL,
	.status	 = NULL,
	.stop	 = NULL,
};

DeviceDriver usb_hid_keyboard_device_driver = {
	.name			   = STRING_INIT("USB HID Keyboard Driver"),
	.priority		   = DRIVER_PRIORITY_GENERAL,
	.type			   = DEVICE_TYPE_USB,
	.private_data_size = 0,
	.ops			   = &usb_hid_keyboard_device_driver_ops,
};
InputDevice usb_hid_keyboard_input_device = {
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

DriverResult usb_hid_keyboard_init(Device *device) {
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
	return DRIVER_RESULT_OK;
}

DriverResult usb_hid_keyboard_start(Device *device) {
	UsbHidKeyboard *keyboard = device->private_data;
	UsbEndpoint	   *ep		 = keyboard->urb->ep;
	ep->data_toggle			 = 1;
	keyboard->usb_device->hcd->ops->add_interrupt_transfer(
		keyboard->usb_device->hcd, keyboard->usb_device, ep, keyboard->urb);
	// UsbControlRequest req = USB_BUILD_REQUEST(
	// 	USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_INTERFACE,
	// 	0x0b, 0x00, 0x00, keyboard->interface->desc->bInterfaceNumber, 0);
	// uint8_t data = 0;
	// keyboard->usb_device->hcd->ops->ctrl_transfer_out(
	// 	keyboard->usb_device->hcd, keyboard->usb_device, &data, 0, &req);
	return DRIVER_RESULT_OK;
}

DriverResult usb_hid_keyboard_probe(
	UsbDevice *usb_device, UsbInterface *interface) {
	Device *device			  = kmalloc(sizeof(Device));
	device->private_data_size = sizeof(UsbHidKeyboard);
	string_new(&device->name, "USB HID Keyboard", 14);
	device->device_driver = &usb_hid_keyboard_device_driver;
	device->ops			  = &usb_hid_keyboard_device_ops;
	device->state		  = DEVICE_STATE_UNREGISTERED;
	interface->usb_driver = &usb_hid_usb_driver;

	register_input_device(
		&usb_hid_keyboard_device_driver, device, usb_device->device->bus,
		&usb_hid_keyboard_input_device);

	UsbHidKeyboard *keyboard = device->private_data;
	keyboard->device		 = device;
	keyboard->usb_device	 = usb_device;
	keyboard->interface		 = interface;

	list_add_tail(&device->new_device_list, &new_device_lh);

	return DRIVER_RESULT_OK;
}

static __init void usb_hid_keyboard_initcall() {
	register_device_driver(&usb_hid_driver, &usb_hid_keyboard_device_driver);
}

driver_initcall(usb_hid_keyboard_initcall);
