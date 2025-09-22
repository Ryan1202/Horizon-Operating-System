#include "kernel/bus_driver.h"
#include "kernel/spinlock.h"
#include "objects/transfer.h"
#include "string.h"
#include <driver/usb/descriptors.h>
#include <driver/usb/urb.h>
#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <driver/usb/usb_driver.h>
#include <drivers/bus/usb.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/dynamic_device_manager.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/softirq.h>
#include <objects/object.h>
#include <stdint.h>

list_t usb_driver_lh[USB_INTERFACE_TYPE_MAX];

void			 usb_dm_probe(DeviceManager *dm);
DriverResult	 usb_dm_load(DeviceManager *manager);
// DriverResult usb_dm_unload(DeviceManager *manager);
UsbDeviceManager usb_dm_ext;

DynamicDeviceEvents usb_dm_events = {
	.probe	= usb_dm_probe,
	.remove = NULL,
};
DeviceManagerOps usb_dm_ops = {
	.dm_load   = usb_dm_load,
	.dm_unload = NULL,
};

// UsbDeviceManager usb_dm_ext;
DeviceManager usb_dm = {
	.type		  = DEVICE_TYPE_USB,
	.ops		  = &usb_dm_ops,
	.private_data = &usb_dm_ext,
};

DriverResult usb_dm_load(DeviceManager *manager) {
	spinlock_init(&usb_dm_ext.hcd_count_lock);
	usb_dm_ext.hcd_count = 0;
	for (int i = 0; i < USB_INTERFACE_TYPE_MAX; i++) {
		list_init(&usb_driver_lh[i]);
	}
	register_dynamic_device_manager(&usb_dm_events, manager);
	softirq_register_handler(SOFTIRQ_USB, usb_softirq_handler);
	return DRIVER_RESULT_OK;
}

void usb_dm_probe(DeviceManager *dm) {
	UsbDriver	 *usb_driver;
	Device		 *device;
	UsbInterface *interface;
	list_for_each_owner (device, &dm->device_lh, dm_list) {
		UsbDevice *usb_device = (UsbDevice *)device->dm_ext;
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
}

DriverResult register_usb_device(
	DeviceDriver *driver, Device *device, UsbDevice *usb_device,
	ObjectAttr *attr) {
	device->dm_ext			= usb_device;
	usb_device->state		= USB_STATE_INITED;
	usb_device->device		= device;
	usb_device->device->ops = &usb_device_ops;
	list_add_tail(&device->dm_list, &usb_dm.device_lh);

	char  _name[4];
	char *next = itoa(_name, device->bus->bus_num, 10);
	*next	   = '\0';

	string_t name;
	string_new(&name, _name, next - _name);
	DRIVER_RESULT_PASS(
		register_device(driver, NULL, device->bus, device, attr));

	// 不可直接传输数据
	device->object->in.type	 = TRANSFER_TYPE_NONE;
	device->object->out.type = TRANSFER_TYPE_NONE;

	return DRIVER_RESULT_OK;
}
