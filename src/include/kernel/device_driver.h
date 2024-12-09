#ifndef _DEVICE_DRIVER_H
#define _DEVICE_DRIVER_H

#include "kernel/device.h"
#include "kernel/driver.h"
#include "kernel/list.h"
#include "stdint.h"
#include "string.h"

// 调用后自动传递错误
#define DRV_OPS_CALL(dm, func, ...)                               \
	{                                                             \
		if ((dm)->ops->func != NULL) {                            \
			DRV_RESULT_DELIVER_CALL((dm)->ops->func, __VA_ARGS__) \
		}                                                         \
	}

typedef enum {
	DRIVER_PRIORITY_BASIC,	   // 基础驱动
	DRIVER_PRIORITY_GENERAL,   // 通用驱动
	DRIVER_PRIORITY_OPTIMIZED, // 优化驱动
	DRIVER_PRIORITY_EXCLUSIVE, // 专属驱动
} DriverPriority;

struct DeviceDriver;

typedef struct DeviceDriverOps {
	DriverResult (*register_driver_hook)(struct DeviceDriver *driver);
	DriverResult (*unregister_driver_hook)(struct DeviceDriver *driver);
} DeviceDriverOps;

typedef enum {
	DRIVER_STATE_UNREGISTERED, // 驱动未注册
	DRIVER_STATE_REGISTERED,   // 驱动已注册
	DRIVER_STATE_ACTIVE,	   // 驱动正在运行
} DriverState;

struct Bus;
typedef struct DeviceDriver {
	// 继承SubDriver特征
	SubDriver driver;

	list_t		   bus_list;
	list_t		   device_lh;
	struct Bus	  *bus;
	string_t	   name;
	DeviceType	   type;
	DriverPriority priority;
	DriverState	   state;

	DeviceDriverOps *ops;

	void	*private_data;
	uint32_t private_data_size;
} DeviceDriver;

extern struct DriverManager device_driver_manager;

DriverResult register_device_driver(
	Driver *driver, DeviceDriver *device_driver);
DriverResult unregister_device_driver(
	Driver *driver, DeviceDriver *device_driver);

#endif