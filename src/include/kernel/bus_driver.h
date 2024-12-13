#ifndef _BUS_DRIVER_H
#define _BUS_DRIVER_H

#include "kernel/device_driver.h"
#include "kernel/driver.h"
#include "kernel/driver_manager.h"
#include "kernel/list.h"
#include "kernel/wait_queue.h"
#include <stdint.h>

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

struct Bus;
typedef struct BusDriverOps {
	DriverResult (*register_bus_hook)(struct Bus *bus);
	DriverResult (*unregister_bus_hook)(struct Bus *bus);

	DriverResult (*init)(struct BusDriver *bus_driver);
} BusDriverOps;

typedef struct BusOps {
	DriverResult (*register_device_hook)(struct DeviceDriver *device_driver);
	DriverResult (*unregister_device_hook)(struct DeviceDriver *device_driver);

	DriverResult (*scan_bus)(struct BusDriver *bus_driver, struct Bus *bus);
} BusOps;

typedef struct BusDriver {
	// 继承SubDriver特征
	SubDriver subdriver;

	list_t		dm_list;
	list_t		bus_lh;
	string_t	name;
	DriverType	driver_type;
	BusType		bus_type;
	DriverState state;

	uint32_t bus_count;
	uint32_t device_count;

	BusDriverOps *ops;

	void	*private_data;
	uint32_t private_data_size;
} BusDriver;

typedef struct Bus {
	list_t		device_lh;
	list_t		bus_list;
	BusDriver  *bus_driver;
	Device	   *controller_device;
	struct Bus *primary_bus;

	uint32_t bus_num;
	uint32_t subordinate_bus_num;

	BusOps *ops;
} Bus;

extern struct BusDriver	   *bus_drivers[BUS_TYPE_MAX];
extern struct DriverManager bus_driver_manager;

DriverResult register_bus_driver(Driver *driver, BusDriver *bus_driver);
DriverResult unregister_bus_driver(Driver *driver, BusType type);
DriverResult register_bus(
	BusDriver *bus_driver, Device *bus_controller_device, Bus *bus);
DriverResult unregister_bus(Bus *bus);
DriverResult bus_register_device(DeviceDriver *device_driver, Bus *bus);
DriverResult bus_unregister_device(DeviceDriver *device_driver);

#endif