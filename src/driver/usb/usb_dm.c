#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>

// DriverResult usb_dm_load(DeviceManager *manager);
// DriverResult usb_dm_unload(DeviceManager *manager);

DeviceManagerOps usb_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,
};

// UsbDeviceManager usb_dm_ext;
DeviceManager usb_dm = {
	.type = DEVICE_TYPE_USB, .ops = &usb_dm_ops,
	// .private_data = &usb_dm_ext,
};