#include "kernel/bus_driver.h"
#include "kernel/spinlock.h"
#include "objects/transfer.h"
#include "string.h"
#include <driver/usb/descriptors.h>
#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <drivers/bus/usb.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>

DriverResult	 usb_dm_load(DeviceManager *manager);
// DriverResult usb_dm_unload(DeviceManager *manager);
UsbDeviceManager usb_dm_ext;

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
	return DRIVER_RESULT_OK;
}

DriverResult register_usb_device(
	DeviceDriver *driver, Device *device, UsbDevice *usb_device,
	ObjectAttr *attr) {
	device->dm_ext			= usb_device;
	usb_device->state		= USB_STATE_UNINITED;
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
