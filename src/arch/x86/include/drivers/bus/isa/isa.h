#ifndef _ISA_H
#define _ISA_H

#include "kernel/bus_driver.h"
#include "kernel/device_driver.h"
#include "kernel/driver.h"
#include "kernel/list.h"

struct IsaDeviceDriver;
typedef struct IsaOps {
	DriverResult (*probe)(struct IsaDeviceDriver *isa_device_driver);
} IsaOps;

typedef struct IsaDeviceDriver {
	list_t		  list;
	DeviceDriver *device_driver;
	IsaOps		 *ops;

	BusDriver *bus_driver;
	Bus		  *bus;
} IsaDeviceDriver;

DriverResult isa_register_device_driver(
	DeviceDriver *device_driver, IsaOps *ops);

#endif