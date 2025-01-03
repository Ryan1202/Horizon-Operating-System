#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <result.h>

DriverResult register_device(
	DeviceDriver *device_driver, Bus *bus, Device *device) {

	device->state = DEVICE_STATE_REGISTERED;

	device->child_devices =
		kmalloc(device->max_child_device * sizeof(ChildDevice));
	device->child_private_data =
		kmalloc(device->max_child_device * sizeof(void *));
	for (int i = 0; i < device->max_child_device; i++) {
		device->child_devices[i].id		  = i;
		device->child_devices[i].is_using = false;
		device->child_devices[i].parent	  = device;
	}

	if (device->private_data_size != 0) {
		device->private_data = kmalloc(device->private_data_size);
	}
	list_add_tail(&device->device_list, &device_driver->device_lh);

	bus_register_device(device_driver, bus);
	return DRIVER_RESULT_OK;
}

DriverResult unregister_device(DeviceDriver *device_driver, Device *device) {
	device->state = DEVICE_STATE_REGISTERED;
	bus_unregister_device(device_driver);
	list_del(&device->device_list);
	return DRIVER_RESULT_OK;
}

DriverResult register_child_device(Device *device, int private_data_size) {
	ChildDevice *new = NULL;
	for (int i = 0; i < device->max_child_device; i++) {
		new = &device->child_devices[i];
		if (!new->is_using) { break; }
	}
	if (new == NULL) { return DRIVER_RESULT_NO_VALID_CHILD_DEVICE; }

	new->is_using						= true;
	new->private_data					= kmalloc(private_data_size);
	device->child_private_data[new->id] = new->private_data;
	return DRIVER_RESULT_OK;
}

DriverResult unregister_child_device(ChildDevice *child_device) {
	Device *device		   = child_device->parent;
	child_device->is_using = false;
	kfree(child_device->private_data);
	device->child_private_data[child_device->id] = NULL;
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
	device->device_driver->subdriver.state = SUBDRIVER_STATE_READY;
	return DRIVER_RESULT_OK;
}

DriverResult init_and_start(Device *device) {
	DRV_RESULT_DELIVER_CALL(init_device, device);
	DRV_RESULT_DELIVER_CALL(start_device, device);
	return DRIVER_RESULT_OK;
}
