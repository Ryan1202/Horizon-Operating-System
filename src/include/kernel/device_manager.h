#ifndef _DEVICE_MANAGER_H
#define _DEVICE_MANAGER_H

#include "kernel/device.h"
#include "kernel/driver.h"
#include "kernel/list.h"

#define DEVM_OPS_CALL(dm, func, ...)                              \
	{                                                             \
		if ((dm)->ops->func != NULL) {                            \
			DRV_RESULT_DELIVER_CALL((dm)->ops->func, __VA_ARGS__) \
		}                                                         \
	}

struct DeviceManager;

typedef struct DeviceMangerOps {
	DriverResult (*dm_load_hook)(struct DeviceManager *manager);
	DriverResult (*dm_unload_hook)(struct DeviceManager *manager);

	DriverResult (*init_device_hook)(
		struct DeviceManager *manager, Device *device);
	DriverResult (*start_device_hook)(
		struct DeviceManager *manager, Device *device);
	DriverResult (*stop_device_hook)(
		struct DeviceManager *manager, Device *device);
	DriverResult (*destroy_device_hook)(
		struct DeviceManager *manager, Device *device);
} DeviceManagerOps;

typedef struct DeviceManager {
	list_t dm_list;
	list_t device_driver_lh;

	DeviceType type;

	DeviceManagerOps *ops;

	void *private_data;
} DeviceManager;

extern DeviceManager *device_managers[DEVICE_TYPE_MAX];

DriverResult register_device_manager(DeviceManager *manager);
DriverResult unregister_device_manager(DeviceManager *manager);

#endif