#include "kernel/device.h"
#include <driver/time_dm.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>

DriverResult timer_dm_load(DeviceManager *manager);
DriverResult timer_dm_unload(DeviceManager *manager);

DeviceManagerOps time_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = NULL,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

TimeDeviceManager time_dm_ext;
DeviceManager	  time_dm = {
		.type		  = DEVICE_TYPE_TIME,
		.ops		  = &time_dm_ops,
		.private_data = &time_dm_ext,
};

DriverResult create_time_device(
	TimeDevice **time_device, TimeOps *time_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops, DEVICE_TYPE_TIME);
	if (result != DRIVER_OK) return result;

	*time_device = kmalloc(sizeof(TimeDevice));
	if (*time_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}

	TimeDevice *time	   = *time_device;
	logical_device->dm_ext = time;
	time->device		   = logical_device;
	time->ops			   = time_ops;
	return DRIVER_OK;
}

DriverResult time_device_start(LogicalDevice *device) {
	TimeDevice *time = (TimeDevice *)device->dm_ext;

	if (time_dm_ext.time_devices[time->device->type] == NULL) {
		time_dm_ext.time_devices[time->device->type] = time;
	}
	return DRIVER_OK;
}

DriverResult delete_time_device(TimeDevice *time_device) {
	int result = 0;

	list_del(&time_device->device->dm_list);
	DRIVER_RESULT_PASS(delete_logical_device(time_device->device));
	result = kfree(time_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

DriverResult get_current_time(TimeType type, Time *time) {
	TimeDevice *time_device = time_dm_ext.time_devices[type];
	if (time_device != NULL) {
		return time_device->ops->get_time(time_device, type, time);
	}
	return DRIVER_ERROR_UNSUPPORT_FEATURE;
}
