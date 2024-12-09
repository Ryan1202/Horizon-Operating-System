#ifndef _DRIVER_DEPENDENCY_H
#define _DRIVER_DEPENDENCY_H

#include "kernel/bus_driver.h"
#include "kernel/device_manager.h"

typedef struct DriverDenpendency {
	enum {
		DRIVER_DEPENDENCY_TYPE_BUS,	   // 依赖某种总线
		DRIVER_DEPENDENCY_TYPE_DEVICE, // 依赖某种设备类型
	} in_type;
	union {
		struct {
			BusType type;
			int		bus_num;
		} dependency_in_bus;
		DeviceType in_device_type;
	};
	union {
		Bus			  *out_bus;
		DeviceManager *device_manager;
	};
} DriverDependency;

DriverResult check_dependency(Driver *driver);

#endif