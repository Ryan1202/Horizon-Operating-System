#ifndef _BUS_DM_H
#define _BUS_DM_H

#include "kernel/bus_driver.h"
#include "kernel/device.h"

typedef struct BusControllerDeviceOps {
	void (*probe)(Device *device);
} BusControllerDeviceOps;

typedef struct BusControllerDevice {
	Device				   *device;
	BusDriver			   *bus_driver;
	BusControllerDeviceOps *bus_controller_ops;
} BusControllerDevice;

DriverResult register_bus_controller_device(
	DeviceDriver *device_driver, BusDriver *bus_driver, Device *device,
	BusControllerDevice *bus_controller_device);

#endif