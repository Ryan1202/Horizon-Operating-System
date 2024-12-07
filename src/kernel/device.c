#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <result.h>

DriverResult register_device(DeviceDriver *device_driver, Device *device) {
	device->state = DEVICE_STATE_REGISTERED;
	list_add_tail(&device->device_list, &device_driver->device_lh);
	bus_register_device(device_driver);
	return DRIVER_RESULT_OK;
}

DriverResult unregister_device(DeviceDriver *device_driver, Device *device) {
	device->state = DEVICE_STATE_REGISTERED;
	bus_unregister_device(device_driver);
	list_del(&device->device_list);
	return DRIVER_RESULT_OK;
}

DriverResult init_device(Device *device) {
	DeviceManager *manager = device_managers[device->device_driver->type];
	if (device->ops->init != NULL) {
		DriverResult result = device->ops->init(device);
		if (result != DRIVER_RESULT_OK) {
			if (result != DRIVER_RESULT_DEVICE_NOT_EXIST) {
				DRV_PRINT_RESULT(result, device->ops->init, device);
				return result;
			} else {
				return DRIVER_RESULT_OK;
			}
		}
	}
	DEVM_OPS_CALL(manager, init_device_hook, manager, device);
	return DRIVER_RESULT_OK;
}

DriverResult start_device(Device *device) {
	DeviceManager *manager = device_managers[device->device_driver->type];
	DEV_OPS_CALL(device, start, device);
	DEVM_OPS_CALL(manager, start_device_hook, manager, device);
	return DRIVER_RESULT_OK;
}

DriverResult init_and_start(Device *device) {
	DRV_RESULT_DELIVER_CALL(init_device, device);
	DRV_RESULT_DELIVER_CALL(start_device, device);
	return DRIVER_RESULT_OK;
}
