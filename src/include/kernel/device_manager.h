#ifndef _DEVICE_MANAGER_H
#define _DEVICE_MANAGER_H

#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/list.h>

struct DeviceManager;

typedef struct DeviceMangerOps {
	DriverResult (*dm_load)(struct DeviceManager *manager);
	DriverResult (*dm_unload)(struct DeviceManager *manager);

	DriverResult (*init_device_hook)(
		struct DeviceManager *manager, LogicalDevice *device);
	DriverResult (*start_device_hook)(
		struct DeviceManager *manager, LogicalDevice *device);
	DriverResult (*stop_device_hook)(
		struct DeviceManager *manager, LogicalDevice *device);
	DriverResult (*destroy_device_hook)(
		struct DeviceManager *manager, LogicalDevice *device);
} DeviceManagerOps;

typedef struct DeviceManager {
	list_t dm_list;
	list_t device_lh;

	DeviceType type;

	DeviceManagerOps *ops;

	void *private_data;
} DeviceManager;

extern DeviceManager *device_managers[DEVICE_TYPE_MAX];

DriverResult init_device_managers();

#endif