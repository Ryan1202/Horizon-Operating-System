#ifndef _USB_FUNC_H
#define _USB_FUNC_H

#include <drivers/usb/usb.h>

#define REQ_GET_PORT_STATUS_SIZE 4

struct usb_device_descriptor *usb_get_device_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep);
struct usb_config_descriptor *usb_get_config_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep);

uint8_t usb_get_config(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep);
usb_setup_status_t usb_set_config(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint8_t config);

struct usb_hub_descriptor *usb_get_hub_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep);

usb_setup_status_t usb_set_address(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint32_t address);

usb_setup_status_t usb_set_port_feature(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint8_t port,
	uint16_t feature);

uint32_t usb_get_hub_status(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep);
uint32_t usb_get_port_status(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint8_t port);

struct usb_string_descriptor *usb_get_string_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, uint8_t index, usb_endpoint_t *ep);

void usb_show_device_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep,
	struct usb_device_descriptor *desc);
void usb_show_hub_descriptor(struct usb_hub_descriptor *desc);

#endif