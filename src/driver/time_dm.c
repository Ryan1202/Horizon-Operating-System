#include "kernel/list.h"
#include <driver/time_dm.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/memory.h>

DriverResult timer_dm_load(DeviceManager *manager);
DriverResult timer_dm_unload(DeviceManager *manager);

DeviceManagerOps time_dm_ops = {
	.dm_load   = timer_dm_load,
	.dm_unload = timer_dm_unload,
};

TimeDeviceManager time_dm_ext;
DeviceManager	  time_dm = {
		.type		  = DEVICE_TYPE_TIME,
		.ops		  = &time_dm_ops,
		.private_data = &time_dm_ext,
};

DriverResult time_dm_load(DeviceManager *manager) {
	manager->private_data = kmalloc(sizeof(TimeDeviceManager));
	return DRIVER_RESULT_OK;
}

DriverResult time_dm_unload(DeviceManager *manager) {
	kfree(manager->private_data);
	return DRIVER_RESULT_OK;
}

DriverResult register_time_device(
	DeviceDriver *driver, Device *device, TimeDevice *time_device) {
	device->dm_ext		= time_device;
	time_device->device = device;

	list_add_tail(&device->dm_list, &time_dm.device_lh);

	if (time_dm_ext.time_devices[time_device->type] == NULL) {
		time_dm_ext.time_devices[time_device->type] = time_device;
	}
	return DRIVER_RESULT_OK;
}

DriverResult get_current_time(TimeType type, Time *time) {
	TimeDevice *time_device = time_dm_ext.time_devices[type];
	if (time_device != NULL) {
		return time_device->ops->get_time(time_device, type, time);
	}
	return DRIVER_RESULT_UNSUPPORT_DEVICE;
}
