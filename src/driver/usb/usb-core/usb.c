#include "kernel/list.h"
#include <bits.h>
#include <driver/timer_dm.h>
#include <drivers/pit.h>
#include <drivers/usb/descriptors.h>
#include <drivers/usb/func.h>
#include <drivers/usb/hub.h>
#include <drivers/usb/usb.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <stdint.h>

UsbDevice *usb_create_device(UsbDeviceSpeed speed, uint8_t address) {
	UsbDevice *device = (UsbDevice *)kmalloc(sizeof(UsbDevice));
	list_init(&device->ep_lh);
	device->desc	= kmalloc(sizeof(struct UsbDeviceDescriptor));
	device->speed	= speed;
	device->address = address;
	return device;
}

int usb_destroy_device(UsbDevice *device) {
	return kfree(device);
}

UsbRequest *usb_create_request(
	uint8_t direction, uint8_t type, uint8_t recipient, uint8_t request_id,
	uint8_t value_hi, uint8_t value_lo, uint16_t index, uint16_t length) {
	UsbRequest *request	   = (UsbRequest *)kmalloc(sizeof(UsbRequest));
	request->bmRequestType = direction << 7 | type << 5 | recipient;
	request->bRequest	   = request_id;
	request->wValue		   = HOST2LE_WORD(value_hi << 8 | value_lo);
	request->wIndex		   = HOST2LE_WORD(index);
	request->wLength	   = HOST2LE_WORD(length);
	return request;
}

UsbEndpoint *usb_create_endpoint(
	UsbHcd *hcd, uint8_t endpoint, UsbEpTransferType transfer_type,
	UsbEpDirection direction, uint16_t max_packet_size) {
	UsbEndpoint *ep		= (UsbEndpoint *)kmalloc(sizeof(UsbEndpoint));
	ep->endpoint		= endpoint;
	ep->transfer_type	= transfer_type;
	ep->direction		= direction;
	ep->max_packet_size = max_packet_size;

	ep->sched = hcd->ops->create_sched();
	return ep;
}

int usb_init_device(UsbHcd *hcd, UsbDevice *device) {
	UsbEndpoint *ep0 =
		usb_create_endpoint(hcd, 0, USB_EP_CONTROL, USB_EP_OUT, 64);
	device->ep0 = ep0;

	struct UsbDeviceDescriptor *desc =
		usb_get_device_descriptor(hcd, device, ep0);
	// usb_show_device_descriptor(hcd, device, ep0, desc);
	hcd->device_count++;
	usb_set_address(hcd, device, ep0, hcd->device_count);

	Timer timer;
	timer_init(&timer);
	delay_ms(&timer, 2);

	device->address = hcd->device_count;
	usb_get_config_descriptor(hcd, device, ep0);

	usb_set_config(hcd, device, ep0, 1);

	if (desc->bDeviceClass == USB_CLASS_HUB) { usb_init_hub(hcd, device); }
	return 0;
}