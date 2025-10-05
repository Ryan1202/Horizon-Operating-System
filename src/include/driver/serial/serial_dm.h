#ifndef _SERIAL_DM_H
#define _SERIAL_DM_H

#include "kernel/driver.h"
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <stdint.h>

typedef enum SerialBaudRate {
	SERIAL_BAUD_115200,
	SERIAL_BAUD_57600,
	SERIAL_BAUD_38400,
	SERIAL_BAUD_19200,
	SERIAL_BAUD_9600,
} SerialBaudRate;

typedef enum SerialReceiveMode {
	SERIAL_RECV_MODE_LOW_LATENCY,
	SERIAL_RECV_MODE_HIGH_THROUGHPUT,
} SerialReceiveMode;

struct SerialDevice;
typedef struct SerialDeviceOps {
	DriverResult (*self_test)(struct SerialDevice *serial);
	void (*set_baud_rate)(
		struct SerialDevice *serial, SerialBaudRate baud_rate);
	void (*set_recv_mode)(
		struct SerialDevice *serial, SerialReceiveMode recv_mode);
} SerialOps;

typedef struct SerialDevice {
	LogicalDevice *device;
	SerialOps	  *ops;

	void (*receive)(uint8_t data);
} SerialDevice;

typedef struct SerialDeviceManager {
	int new_device_num;
	int device_count;
} SerialDeviceManager;

extern DeviceManager serial_dm;

DriverResult create_serial_device(
	SerialDevice **serial_device, SerialOps *serial_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver);
DriverResult delete_serial_device(SerialDevice *serial_device);
DriverResult serial_device_open(
	Object *serial_object, SerialBaudRate baud_rate,
	void (*receive)(uint8_t data));

#endif