#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_manager.h>
#include <kernel/list.h>
#include <kernel/memory.h>

DriverResult device_driver_manager_load(DriverManager *driver_manager);
DriverResult device_driver_manager_unload(DriverManager *driver_manager);
DriverResult driver_load(DeviceDriver *driver);
DriverResult driver_unload(DeviceDriver *driver);

DriverManagerOps device_driver_ops = {
	.dm_load_hook				   = device_driver_manager_load,
	.dm_unload_hook				   = device_driver_manager_unload,
	.register_device_driver_hook   = NULL,
	.unregister_device_driver_hook = NULL,
};

typedef struct DeviceDriverManagerExt {

} DeviceDriverMangerExt;

DeviceDriverMangerExt device_driver_manager_ext;

struct DriverManager device_driver_manager = {
	.type = DRIVER_TYPE_DEVICE_DRIVER,

	.ops = &device_driver_ops,

	.private_data = &device_driver_manager_ext,
};

DriverResult device_driver_manager_load(DriverManager *driver_manager) {
	for (int i = 0; i < DEVICE_TYPE_MAX; i++) {
		DeviceManager *device_manager = device_managers[i];
		if (device_manager != NULL) {
			DEVM_OPS_CALL(device_manager, dm_load_hook, device_manager);
		}
	}

	return DRIVER_RESULT_OK;
}

DriverResult device_driver_manager_unload(DriverManager *driver_manager) {
	for (int i = 0; i < DEVICE_TYPE_MAX; i++) {
		DeviceManager *device_manager = device_managers[i];
		if (device_manager != NULL) {
			DEVM_OPS_CALL(device_manager, dm_unload_hook, device_manager);
		}
	}

	return DRIVER_RESULT_OK;
}

DriverResult register_device_driver(
	Driver *driver, DeviceDriver *device_driver) {

	DriverManager *manager = driver_managers[DRIVER_TYPE_DEVICE_DRIVER];
	if (manager == NULL) return DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST;

	list_init(&device_driver->device_lh);
	device_driver->private_data = kmalloc(device_driver->private_data_size);
	device_driver->state		= DRIVER_STATE_UNINITED;

	DM_OPS_CALL(manager, register_device_driver_hook, manager, device_driver);

	DRV_RESULT_DELIVER_CALL(
		register_sub_driver, driver, &device_driver->subdriver,
		DRIVER_TYPE_DEVICE_DRIVER);

	device_driver->state = DRIVER_STATE_ACTIVE;

	return DRIVER_RESULT_OK;
}

DriverResult unregister_device_driver(
	Driver *driver, DeviceDriver *device_driver) {

	DriverManager *manager = driver_managers[device_driver->type];
	if (manager == NULL) return DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST;

	DRV_RESULT_DELIVER_CALL(
		unregister_sub_driver, driver, &device_driver->subdriver);

	DM_OPS_CALL(manager, unregister_device_driver_hook, manager, device_driver);

	device_driver->state = DRIVER_STATE_UNREGISTERED;
	if (device_driver->private_data != NULL) kfree(device_driver->private_data);

	return DRIVER_RESULT_OK;
}
