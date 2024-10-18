#ifndef HCD_H
#define HCD_H

#include <kernel/driver.h>
#include <kernel/list.h>
#include <stdint.h>
#include <string.h>

struct usb_device;
struct usb_transfer;
struct usb_request;

typedef struct usb_hcd_port {
	uint32_t		port;
	struct usb_hcd *hcd;

	uint8_t suspend;
	uint8_t enable;
	uint8_t connected;
} usb_hcd_port_t;

typedef struct usb_hcd_interface {
	enum usb_setup_status (*control_transaction_in)(
		struct usb_hcd *hcd, struct usb_device *device,
		struct usb_transfer *transfer, void *buffer, uint32_t data_length,
		struct usb_request *usb_req);
	enum usb_setup_status (*control_transaction_out)(
		struct usb_hcd *hcd, struct usb_device *device,
		struct usb_transfer *transfer, void *buffer, uint32_t data_length,
		struct usb_request *usb_req);
} usb_hcd_interface_t;

typedef struct usb_hcd {
	list_t			list;
	usb_hcd_port_t *ports;

	usb_hcd_interface_t *interface;

	string_t *name;
	device_t *device;

	uint8_t device_count;

	list_t usb_devices;
} usb_hcd_t;

usb_hcd_t *usb_hcd_register(
	device_t *device, char *name, uint32_t port_cnt,
	usb_hcd_interface_t *interface);

#endif // HCD_H