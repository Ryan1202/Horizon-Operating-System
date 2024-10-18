#include <bits.h>
#include <drivers/pit.h>
#include <drivers/usb/func.h>
#include <drivers/usb/hub.h>
#include <drivers/usb/usb.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <stdint.h>

usb_device_t *usb_create_device(usb_device_speed_t speed, uint8_t address) {
	usb_device_t *device = (usb_device_t *)kmalloc(sizeof(usb_device_t));
	device->desc		 = kmalloc(sizeof(struct usb_device_descriptor));
	device->speed		 = speed;
	device->address		 = address;
	return device;
}

int usb_destroy_device(usb_device_t *device) {
	return kfree(device);
}

usb_request_t *usb_create_request(
	uint8_t direction, uint8_t type, uint8_t recipient, uint8_t request_id,
	uint8_t value_hi, uint8_t value_lo, uint16_t index, uint16_t length) {
	usb_request_t *request = (usb_request_t *)kmalloc(sizeof(usb_request_t));
	request->bmRequestType = direction << 7 | type << 5 | recipient;
	request->bRequest	   = request_id;
	request->wValue		   = HOST2LE_WORD(value_hi << 8 | value_lo);
	request->wIndex		   = HOST2LE_WORD(index);
	request->wLength	   = HOST2LE_WORD(length);
	return request;
}

usb_endpoint_t usb_default_ep = {
	.endpoint		 = 0,
	.transfer_type	 = USB_EP_CONTROL,
	.direction		 = USB_EP_OUT,
	.max_packet_size = 64,
};

usb_endpoint_t *usb_create_endpoint(
	uint8_t endpoint, enum ep_transfer_type transfer_type,
	enum ep_direction direction, uint16_t max_packet_size) {
	usb_endpoint_t *ep	= (usb_endpoint_t *)kmalloc(sizeof(usb_endpoint_t));
	ep->endpoint		= endpoint;
	ep->transfer_type	= transfer_type;
	ep->direction		= direction;
	ep->max_packet_size = max_packet_size;
	return ep;
}

usb_transfer_t *usb_create_transfer(
	usb_device_t *device, usb_endpoint_t *endpoint) {
	usb_transfer_t *transfer =
		(usb_transfer_t *)kmalloc(sizeof(usb_transfer_t));
	transfer->device = device;
	transfer->ep	 = endpoint;
	return transfer;
}

int usb_init_device(usb_hcd_t *hcd, usb_device_t *device) {
	usb_endpoint_t				  ep = usb_default_ep;
	struct usb_device_descriptor *desc =
		usb_get_device_descriptor(hcd, device, &ep);
	usb_show_device_descriptor(hcd, device, &ep, desc);
	hcd->device_count++;
	usb_set_address(hcd, device, &ep, hcd->device_count);
	delay(2);

	device->address = hcd->device_count;
	usb_get_config_descriptor(hcd, device, &ep);
	usb_set_config(hcd, device, &ep, 1);

	if (desc->bDeviceClass == USB_CLASS_HUB) { usb_init_hub(hcd, device); }
	return 0;
}

usb_setup_status_t usb_control_transaction_in(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, void *buffer,
	usb_request_t *usb_req, uint32_t length) {

	usb_transfer_t *transfer = usb_create_transfer(device, ep);

	return hcd->interface->control_transaction_in(
		hcd, device, transfer, buffer, length, usb_req);
}

usb_setup_status_t usb_control_transaction_out(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, void *buffer,
	usb_request_t *usb_req, uint32_t length) {

	usb_transfer_t *transfer = usb_create_transfer(device, ep);

	return hcd->interface->control_transaction_out(
		hcd, device, transfer, buffer, length, usb_req);
}