#include "kernel/driver.h"
#include "string.h"
#include <driver/serial/serial_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <stdint.h>

string_t serial_object = STRING_INIT("Serial");

DriverResult serial_dm_load(DeviceManager *manager);

DeviceManagerOps serial_dm_ops = {
	.dm_load   = serial_dm_load,
	.dm_unload = NULL,
};
SerialDeviceManager serial_dm_ext;
DeviceManager		serial_dm = {
		  .type			= DEVICE_TYPE_SERIAL,
		  .ops			= &serial_dm_ops,
		  .private_data = &serial_dm_ext,
};

DriverResult serial_dm_load(DeviceManager *manager) {
	serial_dm_ext.device_count = 0;
	return DRIVER_RESULT_OK;
}

DriverResult register_serial_device(
	DeviceDriver *device_driver, Device *device, Bus *bus,
	SerialDevice *serial_device) {
	device->dm_ext		  = serial_device;
	device->device_driver = device_driver;
	serial_device->device = device;

	int		 id = serial_dm_ext.device_count++;
	string_t name;
	string_new_with_number(&name, "Serial", 6, id);
	ObjectAttr attr = device_object_attr;
	register_device(device_driver, &name, bus, device, &attr);

	return DRIVER_RESULT_OK;
}

DriverResult serial_device_open(
	Object *serial_object, SerialBaudRate baud_rate,
	void (*receive)(uint8_t data)) {
	Device		 *device		= serial_object->value.device;
	SerialDevice *serial_device = device->dm_ext;

	DRIVER_RESULT_PASS(init_device(device));

	serial_device->ops->set_baud_rate(serial_device, baud_rate);
	serial_device->ops->set_recv_mode(
		serial_device, SERIAL_RECV_MODE_LOW_LATENCY);
	DRIVER_RESULT_PASS(serial_device->ops->self_test(serial_device));

	serial_device->receive = receive;

	DRIVER_RESULT_PASS(start_device(device));

	return DRIVER_RESULT_OK;
}
