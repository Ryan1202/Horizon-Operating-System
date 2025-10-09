#include "driver/input/key_events.h"
#include <driver/input/input_dm.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/usb/core/descriptors.h>
#include <drivers/usb/hid.h>
#include <drivers/usb/mouse.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <string.h>

DeviceDriver usb_hid_mouse_device_driver;

DriverResult usb_hid_mouse_init(void *_device);
DriverResult usb_hid_mouse_start(void *_device);

DeviceOps usb_hid_mouse_device_ops = {
	.init	 = usb_hid_mouse_init,
	.start	 = usb_hid_mouse_start,
	.destroy = NULL,
	.stop	 = NULL,
};

DeviceDriver usb_hid_mouse_device_driver;
InputDevice	 usb_hid_mouse_input_device = {
	 .type = INPUT_TYPE_MOUSE,
};

void usb_hid_mouse_handler(UsbRequestBlock *urb) {
	UsbHidMouseReport *report	  = (UsbHidMouseReport *)urb->buffer;
	UsbHidMouse		  *mouse	  = urb->context;
	UsbDevice		  *usb_device = mouse->usb_device;
	if (urb->status == USB_STATUS_ACK) {
		if (report->x != 0 || report->y != 0) {
			new_pointer_event(report->x, report->y, POINTER_TYPE_MOVE);
		}
		if ((report->buttons ^ mouse->last_buttons) & 0x01) { // 左键变化
			new_key_event(
				INPUT_KEY_EVENT_MOUSE_BASE + KEY_MOUSE_LEFT,
				report->buttons & 0x01, INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
		}
		if ((report->buttons ^ mouse->last_buttons) & 0x02) { // 右键变化
			new_key_event(
				INPUT_KEY_EVENT_MOUSE_BASE + KEY_MOUSE_RIGHT,
				(report->buttons >> 1) & 0x01, INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
		}
		if ((report->buttons ^ mouse->last_buttons) & 0x04) { // 中键变化
			new_key_event(
				INPUT_KEY_EVENT_MOUSE_BASE + KEY_MOUSE_MIDDLE,
				(report->buttons >> 2) & 0x01, INPUT_KEY_PAGE_KEYBOARD_KEYPAD);
		}
		urb->ep->data_toggle ^= 1;
		usb_device->hcd->ops->interrupt_transfer(usb_device->hcd, urb->ep);
	} else {
		printk("Mouse URB Error: %d\n", urb->status);
	}
}

DriverResult usb_hid_mouse_init(void *_device) {
	LogicalDevice				  *device	 = _device;
	UsbHidMouse					  *mouse	 = device->private_data;
	struct UsbInterfaceDescriptor *interface = mouse->interface->desc;

	UsbEndpoint *ep;
	for (int i = 0; i < interface->bNumEndpoints; i++) {
		ep = mouse->interface->endpoints[i];
		if ((ep->desc->bmAttributes & 0x03) == USB_EP_INTERRUPT &&
			(ep->desc->bEndpointAddress >> 7) == USB_EP_IN) {
			// 找到中断输入端点
			int size	  = ep->desc->wMaxPacketSize & 0x7ff;
			mouse->buffer = kmalloc(size);
			mouse->urb	  = usb_create_urb(
				   ep, mouse, mouse->buffer, size, usb_hid_mouse_handler);
			break;
		}
	}
	return DRIVER_OK;
}

DriverResult usb_hid_mouse_start(void *_device) {
	LogicalDevice *device = _device;
	UsbHidMouse	  *mouse  = device->private_data;
	UsbEndpoint	  *ep	  = mouse->urb->ep;
	ep->data_toggle		  = 1;
	mouse->usb_device->hcd->ops->add_interrupt_transfer(
		mouse->usb_device->hcd, mouse->usb_device, ep, mouse->urb);
	return DRIVER_OK;
}

DriverResult usb_hid_mouse_probe(
	UsbDevice *usb_device, UsbInterface *interface) {
	PhysicalDevice *physical_device = usb_device->device;

	interface->usb_driver = &usb_hid_usb_driver;

	InputDevice *input_device;
	DRIVER_RESULT_PASS(create_input_device(
		&input_device, INPUT_TYPE_MOUSE, &usb_hid_mouse_device_ops,
		physical_device, &usb_hid_mouse_device_driver));

	UsbHidMouse *mouse				   = kmalloc(sizeof(UsbHidMouse));
	mouse->device					   = input_device;
	mouse->usb_device				   = usb_device;
	mouse->interface				   = interface;
	input_device->device->private_data = mouse;

	usb_device->state = USB_STATE_ACTIVE;

	return DRIVER_OK;
}

static __init void usb_hid_mouse_initcall() {
	register_device_driver(&usb_hid_driver, &usb_hid_mouse_device_driver);
}

driver_initcall(usb_hid_mouse_initcall);
