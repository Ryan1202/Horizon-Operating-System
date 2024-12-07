#ifndef _BUS_DRIVER_H
#define _BUS_DRIVER_H

#include "kernel/device_driver.h"
#include "kernel/driver.h"
#include "kernel/driver_manager.h"

#define BUS_OPS_CALL(bus, func, ...)                               \
	{                                                              \
		if ((bus)->ops->func != NULL) {                            \
			DRV_RESULT_DELIVER_CALL((bus)->ops->func, __VA_ARGS__) \
		}                                                          \
	}

struct BusDriver;

typedef enum BusType {
	BUS_TYPE_PLATFORM,
	BUS_TYPE_PCI,
	BUS_TYPE_ISA,
	BUS_TYPE_USB,
	BUS_TYPE_MAX,
} BusType;

typedef struct BusDriverOps {
	DriverResult (*register_device_hook)(struct DeviceDriver *device_driver);
	DriverResult (*unregister_device_hook)(struct DeviceDriver *device_driver);
} BusDriverOps;

typedef struct BusDriver {
	// 继承SubDriver特征
	SubDriver driver;

	list_t		dm_list;
	list_t		device_lh;
	string_t	name;
	DriverType	driver_type;
	BusType		bus_type;
	DriverState state;

	BusDriverOps *ops;

	void	*private_data;
	uint32_t private_data_size;
} BusDriver;

extern struct DriverManager bus_driver_manager;

DriverResult register_bus_driver(Driver *driver, BusDriver *bus_driver);
DriverResult unregister_bus_driver(Driver *driver, BusType type);
DriverResult bus_register_device(DeviceDriver *device_driver);
DriverResult bus_unregister_device(DeviceDriver *device_driver);

#endif