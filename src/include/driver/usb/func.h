#ifndef _USB_FUNC_H
#define _USB_FUNC_H

#include <driver/usb/usb.h>

#define REQ_GET_PORT_STATUS_SIZE 4

void *usb_get_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, uint8_t type, uint8_t recipient,
	uint8_t index, UsbEndpoint *ep, uint8_t value_hi, uint8_t value_lo,
	uint16_t desc_size);
#define usb_get_standard_descriptor(                                        \
	hcd, device, recipient, index, ep, value_hi, value_lo, desc_size)       \
	usb_get_descriptor(                                                     \
		hcd, device, USB_REQ_TYPE_STANDARD, recipient, index, ep, value_hi, \
		value_lo, desc_size)
#define usb_get_class_descriptor(                                        \
	hcd, device, recipient, index, ep, value_hi, value_lo, desc_size)    \
	usb_get_descriptor(                                                  \
		hcd, device, USB_REQ_TYPE_CLASS, recipient, index, ep, value_hi, \
		value_lo, desc_size)
#define usb_get_vendor_descriptor(                                        \
	hcd, device, recipient, index, ep, value_hi, value_lo, desc_size)     \
	usb_get_descriptor(                                                   \
		hcd, device, USB_REQ_TYPE_VENDOR, recipient, index, ep, value_hi, \
		value_lo, desc_size)

#define usb_get_device_descriptor(hcd, device, ep)                             \
	usb_get_standard_descriptor(                                               \
		hcd, device, USB_REQ_RECIPIENT_DEVICE, 0, ep, USB_DESC_TYPE_DEVICE, 0, \
		sizeof(struct UsbDeviceDescriptor))
#define usb_get_interface_descriptor(hcd, device, ep)                          \
	usb_get_standard_descriptor(                                               \
		hcd, device, USB_REQ_RECIPIENT_DEVICE, 0, ep, USB_DESC_TYPE_INTERFACE, \
		0, sizeof(struct UsbInterfaceDescriptor))

void *usb_set_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, uint8_t type, uint8_t recipient,
	uint8_t index, UsbEndpoint *ep, uint8_t value_hi, uint8_t value_lo,
	uint16_t desc_size);
#define usb_set_standard_descriptor(                                        \
	hcd, device, recipient, index, ep, value_hi, value_lo, desc_size)       \
	usb_set_descriptor(                                                     \
		hcd, device, USB_REQ_TYPE_STANDARD, recipient, index, ep, value_hi, \
		value_lo, desc_size)
#define usb_set_class_descriptor(                                        \
	hcd, device, recipient, index, ep, value_hi, value_lo, desc_size)    \
	usb_set_descriptor(                                                  \
		hcd, device, USB_REQ_TYPE_CLASS, recipient, index, ep, value_hi, \
		value_lo, desc_size)
#define usb_set_vendor_descriptor(                                        \
	hcd, device, recipient, index, ep, value_hi, value_lo, desc_size)     \
	usb_set_descriptor(                                                   \
		hcd, device, USB_REQ_TYPE_VENDOR, recipient, index, ep, value_hi, \
		value_lo, desc_size)

struct UsbConfigDescriptor *usb_get_config_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep);

uint8_t usb_get_config(UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep);
UsbSetupStatus usb_set_config(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep, uint8_t config);

struct UsbHubDescriptor *usb_get_hub_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep);

UsbSetupStatus usb_set_address(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep, uint32_t address);

UsbSetupStatus usb_set_port_feature(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep, uint8_t port,
	uint16_t feature);

uint32_t usb_get_hub_status(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep);
uint32_t usb_get_port_status(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep, uint8_t port);

struct UsbStringDescriptor *usb_get_string_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, uint8_t index, UsbEndpoint *ep);

void usb_show_device_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep,
	struct UsbDeviceDescriptor *desc);
void usb_show_hub_descriptor(struct UsbHubDescriptor *desc);

#endif