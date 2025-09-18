#ifndef _BUS_USB_H
#define _BUS_USB_H

#include <kernel/bus_driver.h>
#include <kernel/driver.h>
#include <kernel/list.h>

extern BusDriver usb_bus_driver;
extern Driver	 usb_driver;
extern list_t	 hci_lh;
extern DeviceOps usb_device_ops;

typedef struct HciInit {
	list_t list;
	void (*init)(Driver *driver);
} HciInit;

#endif