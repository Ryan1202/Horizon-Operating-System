#ifndef _DEVICE_DRIVER_H
#define _DEVICE_DRIVER_H

#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/list.h>

typedef enum {
	DRIVER_PRIORITY_BASIC,	   // 基础驱动
	DRIVER_PRIORITY_GENERAL,   // 通用驱动
	DRIVER_PRIORITY_OPTIMIZED, // 优化驱动
	DRIVER_PRIORITY_EXCLUSIVE, // 专属驱动
} DriverPriority;

struct Bus;
struct Object;
typedef struct DeviceDriver {
	list_t device_driver_list;
	list_t device_lh;
} DeviceDriver;

DriverResult register_device_driver(
	Driver *driver, DeviceDriver *device_driver);
DriverResult unregister_device_driver(DeviceDriver *device_driver);

#endif