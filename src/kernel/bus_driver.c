#include <kernel/bus_driver.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_manager.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/wait_queue.h>

BusDriver			*bus_drivers[BUS_TYPE_MAX];
wait_queue_manager_t bus_wqm[BUS_TYPE_MAX];

DriverResult bus_driver_manager_load(DriverManager *driver_manager);
DriverResult bus_driver_manager_unload(DriverManager *driver_manager);
DriverResult driver_load(BusDriver *driver);
DriverResult driver_unload(BusDriver *driver);

DriverManagerOps bus_driver_ops = {
	.dm_load_hook				   = bus_driver_manager_load,
	.dm_unload_hook				   = bus_driver_manager_unload,
	.register_device_driver_hook   = NULL,
	.unregister_device_driver_hook = NULL,
};

typedef struct BusDriverManagerExt {
} BusDriverMangerExt;

BusDriverMangerExt bus_driver_manager_ext;

struct DriverManager bus_driver_manager = {
	.type = DRIVER_TYPE_BUS_DRIVER,

	.ops = &bus_driver_ops,

	.private_data = &bus_driver_manager_ext,
};

DriverResult bus_driver_manager_load(DriverManager *driver_manager) {
	for (int i = 0; i < BUS_TYPE_MAX; i++) {
		wait_queue_init(&bus_wqm[i]);
	}
	return DRIVER_RESULT_OK;
}

DriverResult bus_driver_manager_unload(DriverManager *driver_manager) {

	return DRIVER_RESULT_OK;
}

DriverResult register_bus_driver(Driver *driver, BusDriver *bus_driver) {

	DriverManager *manager = driver_managers[DRIVER_TYPE_BUS_DRIVER];
	if (manager == NULL) return DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST;

	BusDriver *_bus_driver = bus_drivers[bus_driver->driver_type];
	if (_bus_driver != NULL) return DRIVER_RESULT_BUS_DRIVER_ALREADY_EXIST;

	bus_driver->private_data = kmalloc(bus_driver->private_data_size);
	bus_driver->state		 = DRIVER_STATE_REGISTERED;

	DRV_RESULT_DELIVER_CALL(register_sub_driver, driver, &bus_driver->driver);

	bus_drivers[bus_driver->bus_type] = bus_driver;

	return DRIVER_RESULT_OK;
}

DriverResult unregister_bus_driver(Driver *driver, BusType type) {

	DriverManager *manager = driver_managers[DRIVER_TYPE_BUS_DRIVER];
	if (manager == NULL) return DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST;

	BusDriver *bus_driver = bus_drivers[type];
	if (bus_driver == NULL) return DRIVER_RESULT_BUS_DRIVER_NOT_EXIST;

	bus_drivers[bus_driver->driver_type] = NULL;

	Bus *cur, *next;
	list_for_each_owner_safe (cur, next, &bus_driver->bus_lh, bus_list) {
		unregister_bus(cur);
	}

	DRV_RESULT_DELIVER_CALL(unregister_sub_driver, driver, &bus_driver->driver);

	bus_driver->state = DRIVER_STATE_UNREGISTERED;
	if (bus_driver->private_data != NULL) kfree(bus_driver->private_data);

	return DRIVER_RESULT_OK;
}

DriverResult register_bus(
	BusDriver *bus_driver, Device *bus_controller_device, Bus *bus) {
	if (bus_driver == NULL) return DRIVER_RESULT_BUS_DRIVER_NOT_EXIST;

	bus->bus_driver		   = bus_driver;
	bus->controller_device = bus_controller_device;
	list_init(&bus->device_lh);
	list_add_tail(&bus->bus_list, &bus_driver->bus_lh);
	BUS_OPS_CALL(bus_driver, register_bus_hook, bus);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_bus(Bus *bus) {
	BusDriver *bus_driver = bus->bus_driver;
	if (bus_driver == NULL) return DRIVER_RESULT_BUS_DRIVER_NOT_EXIST;

	// 取消注册bus下的所有device_driver
	DeviceDriver *cur, *next;
	list_for_each_owner_safe (cur, next, &bus->device_lh, bus_list) {
		bus_unregister_device(cur);
	}

	BUS_OPS_CALL(bus_driver, unregister_bus_hook, bus);
	list_del(&bus->bus_list);
	bus->bus_driver = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult bus_register_device(DeviceDriver *device_driver, Bus *bus) {
	BusDriver *bus_driver = bus_drivers[device_driver->type];
	if (bus_driver == NULL) return DRIVER_RESULT_BUS_DRIVER_NOT_EXIST;

	device_driver->bus = bus;
	list_add_tail(&device_driver->bus_list, &bus->device_lh);
	BUS_OPS_CALL(bus, register_device_hook, device_driver);

	return DRIVER_RESULT_OK;
}

DriverResult bus_unregister_device(DeviceDriver *device_driver) {
	Bus *bus = device_driver->bus;
	if (bus == NULL) return DRIVER_RESULT_BUS_DRIVER_NOT_EXIST;

	BUS_OPS_CALL(bus, unregister_device_hook, device_driver);
	list_del(&device_driver->bus_list);
	device_driver->bus = NULL;

	return DRIVER_RESULT_OK;
}
