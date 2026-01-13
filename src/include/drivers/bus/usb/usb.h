#ifndef _BUS_USB_H
#define _BUS_USB_H

#include <drivers/bus/usb/hcd.h>
#include <drivers/usb/core/usb.h>
#include <kernel/bus_driver.h>
#include <kernel/driver.h>
#include <kernel/list.h>

typedef struct UsbDevice {
	list_t	list;
	uint8_t address;

	list_t ep_lh;
	list_t interface_lh;

	UsbDeviceState	state;
	UsbHcd		   *hcd;
	PhysicalDevice *device;
	struct UsbHub  *hub;

	struct UsbDeviceDescriptor *desc;
	UsbDeviceSpeed				speed;
	struct UsbEndpoint		   *ep0;

	void *private_data;
} UsbDevice;

extern BusDriver usb_bus_driver;
extern Driver	 usb_driver;
extern DeviceOps usb_device_ops;

#endif