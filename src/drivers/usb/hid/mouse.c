#include "driver/usb/descriptors.h"
#include "driver/usb/usb_dm.h"
#include "kernel/dynamic_device_manager.h"
#include "kernel/list.h"
#include <driver/usb/usb.h>
#include <drivers/usb/hid.h>
#include <drivers/usb/mouse.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <string.h>

DeviceDriver usb_hid_mouse_device_driver;

DriverResult usb_hid_mouse_init(Device *device);
DriverResult usb_hid_mouse_start(Device *device);

DeviceDriverOps usb_hid_mouse_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
DeviceOps usb_hid_mouse_device_ops = {
	.init	 = usb_hid_mouse_init,
	.start	 = usb_hid_mouse_start,
	.destroy = NULL,
	.status	 = NULL,
	.stop	 = NULL,
};

DeviceDriver usb_hid_mouse_device_driver = {
	.name			   = STRING_INIT("USB HID Mouse Driver"),
	.priority		   = DRIVER_PRIORITY_GENERAL,
	.type			   = DEVICE_TYPE_USB,
	.private_data_size = 0,
	.ops			   = &usb_hid_mouse_device_driver_ops,
};

void usb_hid_mouse_handler(UsbRequestBlock *urb) {
	UsbHidMouseReport *report	  = (UsbHidMouseReport *)urb->buffer;
	UsbHidMouse		  *mouse	  = urb->context;
	UsbDevice		  *usb_device = mouse->usb_device;
	if (urb->status == USB_STATUS_ACK) {
		printk(
			"Mouse Report: Buttons: 0x%02x, X: %d, Y: %d\n", report->buttons,
			report->x, report->y);
		urb->ep->data_toggle ^= 1;
		usb_device->hcd->ops->interrupt_transfer(usb_device->hcd, urb->ep);
	} else {
		printk("Mouse URB Error: %d\n", urb->status);
	}
}

DriverResult usb_hid_mouse_init(Device *device) {
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
	return DRIVER_RESULT_OK;
}

DriverResult usb_hid_mouse_start(Device *device) {
	UsbHidMouse *mouse = device->private_data;
	UsbEndpoint *ep	   = mouse->urb->ep;
	ep->data_toggle	   = 1;
	mouse->usb_device->hcd->ops->add_interrupt_transfer(
		mouse->usb_device->hcd, mouse->usb_device, ep, mouse->urb);
	return DRIVER_RESULT_OK;
}

DriverResult usb_hid_mouse_probe(
	UsbDevice *usb_device, UsbInterface *interface) {
	Device *device			  = kmalloc(sizeof(Device));
	device->private_data_size = sizeof(UsbHidMouse);
	string_new(&device->name, "USB HID Mouse", 14);
	device->device_driver = &usb_hid_mouse_device_driver;
	device->ops			  = &usb_hid_mouse_device_ops;
	device->state		  = DEVICE_STATE_UNREGISTERED;
	interface->usb_driver = &usb_hid_usb_driver;

	ObjectAttr attr = device_object_attr;
	register_device(
		&usb_hid_mouse_device_driver, &device->name, usb_device->device->bus,
		device, &attr);

	UsbHidMouse *mouse = device->private_data;
	mouse->device	   = device;
	mouse->usb_device  = usb_device;
	mouse->interface   = interface;

	list_add_tail(&device->new_device_list, &new_device_lh);

	return DRIVER_RESULT_OK;
}

static __init void usb_hid_mouse_initcall() {
	register_device_driver(&usb_hid_driver, &usb_hid_mouse_device_driver);
}

driver_initcall(usb_hid_mouse_initcall);
