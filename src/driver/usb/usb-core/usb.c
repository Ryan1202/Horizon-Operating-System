#include "kernel/device.h"
#include "kernel/list.h"
#include "objects/object.h"
#include <bits.h>
#include <driver/timer_dm.h>
#include <driver/usb/descriptors.h>
#include <driver/usb/func.h>
#include <driver/usb/hub.h>
#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <drivers/pit.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <stdint.h>

UsbDevice *usb_create_device(
	UsbHcd *hcd, UsbDeviceSpeed speed, uint8_t address) {
	Device *device			  = kmalloc(sizeof(Device));
	device->private_data_size = 0;
	device->ops				  = NULL;
	device->state			  = DEVICE_STATE_UNREGISTERED;
	device->bus				  = hcd->bus;

	UsbDevice *usb_device = (UsbDevice *)kmalloc(sizeof(UsbDevice));
	list_init(&usb_device->ep_lh);
	list_init(&usb_device->interface_lh);
	usb_device->desc	= kmalloc(sizeof(struct UsbDeviceDescriptor));
	usb_device->speed	= speed;
	usb_device->address = address;
	usb_device->state	= USB_STATE_UNINITED;
	usb_device->device	= device;
	usb_device->hcd		= hcd;

	return usb_device;
}

int usb_destroy_device(UsbDevice *device) {
	return kfree(device);
}

UsbControlRequest *usb_create_request(
	uint8_t direction, uint8_t type, uint8_t recipient, uint8_t request_id,
	uint8_t value_hi, uint8_t value_lo, uint16_t index, uint16_t length) {
	UsbControlRequest *request =
		(UsbControlRequest *)kmalloc(sizeof(UsbControlRequest));
	request->bmRequestType = direction << 7 | type << 5 | recipient;
	request->bRequest	   = request_id;
	request->wValue		   = HOST2LE_WORD(value_hi << 8 | value_lo);
	request->wIndex		   = HOST2LE_WORD(index);
	request->wLength	   = HOST2LE_WORD(length);
	return request;
}

void usb_init_endpoint(
	UsbDevice *usb_device, UsbEndpoint *ep,
	struct UsbEndpointDescriptor *desc) {
	ep->desc = desc;

	ep->pipe = usb_device->hcd->ops->create_pipeline(usb_device, ep);
}

int usb_init_device(
	UsbHcd *hcd, UsbEndpoint *ep0, struct UsbEndpointDescriptor *ep_desc,
	UsbDevice *usb_device) {
	usb_init_endpoint(usb_device, ep0, ep_desc);
	usb_device->ep0 = ep0;

	struct UsbDeviceDescriptor *desc =
		usb_get_device_descriptor(hcd, usb_device, ep0);
	// usb_show_device_descriptor(hcd, device, ep0, desc);
	hcd->device_count++;
	usb_set_address(hcd, usb_device, ep0, hcd->device_count);

	Timer timer;
	timer_init(&timer);
	delay_ms(&timer, 2);

	usb_device->address = hcd->device_count;
	usb_get_config_descriptor(hcd, usb_device, ep0);

	usb_set_config(hcd, usb_device, ep0, 1);

	ObjectAttr attr = device_object_attr;
	register_usb_device(
		hcd->device->device_driver, usb_device->device, usb_device, &attr);

	if (desc->bDeviceClass == USB_CLASS_HUB) {
		usb_init_hub(hcd, ep0, usb_device);
	}
	return 0;
}