#include <driver/serial/serial_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

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
	serial_dm_ext.new_device_num = 0;
	serial_dm_ext.device_count	 = 0;
	return DRIVER_OK;
}

DriverResult create_serial_device(
	SerialDevice **serial_device, SerialOps *serial_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	LogicalDevice *logical_device = NULL;

	DRIVER_RESULT_PASS(create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_SERIAL));

	*serial_device = kzalloc(sizeof(SerialDevice));
	if (*serial_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	SerialDevice *serial   = *serial_device;
	logical_device->dm_ext = serial;
	serial->device		   = logical_device;
	serial->ops			   = serial_ops;

	char	 _name[] = "Serial";
	string_t name;
	string_new_with_number(
		&name, _name, sizeof(_name) - 1, serial_dm_ext.new_device_num);
	serial_dm_ext.new_device_num++;
	serial_dm_ext.device_count++;

	logical_device->object =
		create_object(&device_object, &name, device_object_attr);
	if (logical_device->object == NULL) {
		kfree(serial);
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OBJECT;
	}

	Object *obj				  = logical_device->object;
	obj->value.device.kind	  = DEVICE_KIND_LOGICAL;
	obj->value.device.logical = logical_device;

	return DRIVER_OK;
}

DriverResult delete_serial_device(SerialDevice *serial_device) {
	serial_dm_ext.device_count--;

	LogicalDevice *logical_device = serial_device->device;

	delete_logical_device(logical_device);
	int result = kfree(serial_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

DriverResult serial_device_open(
	Object *serial_object, SerialBaudRate baud_rate,
	void (*receive)(uint8_t data)) {
	LogicalDevice *device		 = serial_object->value.device.logical;
	SerialDevice  *serial_device = device->dm_ext;

	DRIVER_RESULT_PASS(init_logical_device(device));

	serial_device->ops->set_baud_rate(serial_device, baud_rate);
	serial_device->ops->set_recv_mode(
		serial_device, SERIAL_RECV_MODE_LOW_LATENCY);
	DRIVER_RESULT_PASS(serial_device->ops->self_test(serial_device));

	serial_device->receive = receive;

	DRIVER_RESULT_PASS(start_logical_device(device));

	return DRIVER_OK;
}
