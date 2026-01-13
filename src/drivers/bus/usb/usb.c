#include "kernel/device.h"
#include "kernel/spinlock.h"
#include <driver/bus_dm.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/bus/usb/usb_driver.h>
#include <drivers/usb/core/urb.h>
#include <kernel/bus_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>
#include <kernel/list.h>
#include <kernel/platform.h>
#include <kernel/softirq.h>

#define LH_INIT(type) [type] = LIST_HEAD_INIT(usb_driver_lh[type])

DriverResult usb_probe(BusDriver *bus_driver, Bus *bus);

list_t usb_driver_lh[USB_INTERFACE_TYPE_MAX] = {
	LH_INIT(USB_INTERFACE_TYPE_HID),
	LH_INIT(USB_INTERFACE_TYPE_MASS_STORAGE),
	LH_INIT(USB_INTERFACE_TYPE_HUB),
	LH_INIT(USB_INTERFACE_TYPE_CDC_DATA),
	LH_INIT(USB_INTERFACE_TYPE_SMART_CARD),
	LH_INIT(USB_INTERFACE_TYPE_CONTENT_SECURITY),
	LH_INIT(USB_INTERFACE_TYPE_VIDEO),
	LH_INIT(USB_INTERFACE_TYPE_PERSONAL_HEALTHCARE),
	LH_INIT(USB_INTERFACE_TYPE_AUDIO_VIDEO),
	LH_INIT(USB_INTERFACE_TYPE_BILLBOARD),
	LH_INIT(USB_INTERFACE_TYPE_TYPE_C_BRIDGE),
	LH_INIT(USB_INTERFACE_TYPE_BULK_DISPLAY_PROTOCOL),
	LH_INIT(USB_INTERFACE_TYPE_MTCP),
	LH_INIT(USB_INTERFACE_TYPE_I3C),
	LH_INIT(USB_INTERFACE_TYPE_DIAGNOSTIC),
	LH_INIT(USB_INTERFACE_TYPE_WIRELESS_CONTROLLER),
	LH_INIT(USB_INTERFACE_TYPE_MISCELLANEOUS),
	LH_INIT(USB_INTERFACE_TYPE_APPLICATION_SPECIFIC),
	LH_INIT(USB_INTERFACE_TYPE_VENDOR_SPECIFIC),
};

BusOps usb_bus_ops = {
	.scan_bus	  = NULL,
	.probe_device = usb_probe,
};

Driver usb_driver = {
	.short_name = STRING_INIT("UsbDriver"),
};
BusDriver usb_bus_driver = {
	.name = STRING_INIT("USB"),
};

DriverResult usb_probe(BusDriver *bus_driver, Bus *bus) {
	UsbDriver	   *usb_driver;
	PhysicalDevice *device;
	UsbInterface   *interface;
	list_for_each_owner (device, &bus->device_lh, device_list) {
		UsbDevice *usb_device = device->bus_ext;
		if (usb_device->state == USB_STATE_INITED) {
			list_for_each_owner (interface, &usb_device->interface_lh, list) {
				if (interface->usb_driver != NULL) { continue; }
				uint8_t interface_type =
					usb_interface_map[interface->desc->bInterfaceClass];
				list_for_each_owner (
					usb_driver, &usb_driver_lh[interface_type], list) {
					if (usb_driver->probe != NULL) {
						usb_driver->probe(usb_device, interface);
					}
				}
			}
		}
	}

	return DRIVER_OK;
}

DriverResult register_usb_driver(UsbDriver *usb_driver) {
	list_add_tail(
		&usb_driver->list, &usb_driver_lh[usb_driver->interface_type]);
	return DRIVER_OK;
}

DriverResult unregister_usb_driver(UsbDriver *usb_driver) {
	list_del(&usb_driver->list);
	return DRIVER_OK;
}

static __init void usb_bus_driver_entry(void) {
	DriverResult result;

	result = register_driver(&usb_driver);
	if (result != DRIVER_OK) goto failed_register_driver;

	ObjectAttr attr = driver_object_attr;
	result =
		register_bus_driver(&usb_driver, BUS_TYPE_USB, &usb_bus_driver, &attr);
	if (result != DRIVER_OK) goto failed_register_bus_driver;

	softirq_register_handler(SOFTIRQ_USB, usb_softirq_handler);

	return;
failed_register_bus_driver:
	unregister_driver(&usb_driver);

failed_register_driver:
	return;
}

driver_initcall(usb_bus_driver_entry);
