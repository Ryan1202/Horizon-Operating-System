#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>

DriverResult driver_load(DeviceDriver *driver);
DriverResult driver_unload(DeviceDriver *driver);

DriverResult register_device_driver(
	Driver *driver, DeviceDriver *device_driver) {
	list_init(&device_driver->device_lh);
	list_add_tail(
		&device_driver->device_driver_list, &driver->device_driver_lh);
	return DRIVER_OK;
}

DriverResult unregister_device_driver(DeviceDriver *device_driver) {
	if (!list_empty(&device_driver->device_lh)) return DRIVER_ERROR_BUSY;
	list_del(&device_driver->device_driver_list);

	return DRIVER_OK;
}
