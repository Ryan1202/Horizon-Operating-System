#ifndef _USB_DEVICE_DRIVER_H
#define _USB_DEVICE_DRIVER_H

#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <kernel/list.h>

typedef struct UsbDriver {
	list_t list;

	Driver			*driver;
	UsbInterfaceType interface_type;

	DriverResult (*probe)(
		struct UsbDevice *device, struct UsbInterface *interface);
	DriverResult (*remove)(struct UsbDevice *device);
} UsbDriver;

extern list_t usb_driver_lh[USB_INTERFACE_TYPE_MAX];

DriverResult register_usb_driver(UsbDriver *usb_driver);

#endif