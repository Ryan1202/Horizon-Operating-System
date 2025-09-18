#ifndef _USB_FUNC_H
#define _USB_FUNC_H

#include <driver/usb/usb.h>

#define REQ_GET_PORT_STATUS_SIZE 4

struct UsbDeviceDescriptor *usb_get_device_descriptor(
	UsbHcd *hcd, struct UsbDevice *device, UsbEndpoint *ep);
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