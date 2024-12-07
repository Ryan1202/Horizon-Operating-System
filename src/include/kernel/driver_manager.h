#ifndef _DRIVER_MANAGER_H
#define _DRIVER_MANAGER_H

#include "kernel/device_driver.h"
#include "kernel/driver.h"
#include "kernel/list.h"
#include "result.h"

// 调用后自动传递错误
#define DM_OPS_CALL(dm, func, ...)                                \
	{                                                             \
		if ((dm)->ops->func != NULL) {                            \
			DRV_RESULT_DELIVER_CALL((dm)->ops->func, __VA_ARGS__) \
		}                                                         \
	}

struct DriverManager;

typedef struct DriverManagerOps {
	DriverResult (*dm_load_hook)(struct DriverManager *driver_manager);
	DriverResult (*register_device_driver_hook)(
		struct DriverManager *driver_manager, struct DeviceDriver *driver);
	DriverResult (*unregister_device_driver_hook)(
		struct DriverManager *driver_manager, struct DeviceDriver *driver);
	DriverResult (*dm_unload_hook)(struct DriverManager *driver_manager);
} DriverManagerOps;

typedef struct DriverManager {
	list_t			  dm_lh;
	DriverType		  type;
	DriverManagerOps *ops;

	void *private_data;
} DriverManager;

extern DriverManager *driver_managers[DRIVER_TYPE_MAX];

DriverResult register_driver_manager(DriverManager *driver_manager);
DriverResult unregister_driver_manager(DriverManager *driver_manager);

#endif