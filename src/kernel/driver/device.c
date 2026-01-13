#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <result.h>
#include <string.h>

DriverResult create_physical_device(
	PhysicalDevice **physical_device, Bus *bus, ObjectAttr *attr) {

	*physical_device = kmalloc(sizeof(PhysicalDevice));
	if (*physical_device == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;
	PhysicalDevice *phy = *physical_device;

	phy->kind  = DEVICE_KIND_PHYSICAL;
	phy->bus   = bus;
	phy->ops   = NULL;
	phy->state = DEVICE_STATE_UNINIT;
	list_add_tail(&phy->device_list, &bus->device_lh);
	list_init(&phy->logical_device_lh);

	phy->private_data = NULL;
	phy->bus_ext	  = NULL;

	char _name[6] = {0}; // device_count为uint16_t类型，最大65535，5位数
	itoa(_name, bus->new_device_num++, 10);
	bus->device_count++;

	string_t name;
	string_new(&name, _name, sizeof(_name));
	attr->type					   = OBJECT_TYPE_DEVICE;
	phy->object					   = create_object(bus->object, &name, *attr);
	phy->object->value.device.kind = DEVICE_KIND_PHYSICAL;
	phy->object->value.device.physical = phy;

	return DRIVER_OK;
}

void register_physical_device(PhysicalDevice *physical_device, DeviceOps *ops) {
	physical_device->ops = ops;
}

DriverResult delete_physical_device(PhysicalDevice *physical_device) {
	Bus			*bus = physical_device->bus;
	DriverResult ret = DRIVER_OK;

	if (!list_empty(&physical_device->logical_device_lh)) {
		return DRIVER_ERROR_BUSY;
	}
	if (physical_device->object) {
		ObjectResult result = delete_object(physical_device->object);
		if (result != OBJECT_OK) ret = DRIVER_ERROR_OBJECT;
	}

	list_del(&physical_device->device_list);
	int result = kfree(physical_device);
	if (result < 0) ret = DRIVER_ERROR_MEMORY_FREE;

	bus->device_count--;
	return ret;
}

DriverResult create_logical_device(
	LogicalDevice **logical_device, PhysicalDevice *physical_device,
	DeviceDriver *device_driver, DeviceOps *ops, DeviceType type) {
	*logical_device = kmalloc(sizeof(LogicalDevice));
	if (logical_device == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;
	LogicalDevice *logi = *logical_device;

	logi->kind			  = DEVICE_KIND_LOGICAL;
	logi->state			  = DEVICE_STATE_UNINIT;
	logi->ops			  = ops;
	logi->type			  = type;
	logi->physical_device = physical_device;
	list_add_tail(
		&logi->logical_device_list, &physical_device->logical_device_lh);

	if (device_managers[type] != NULL) {
		DeviceManager *manager = device_managers[type];
		list_add_tail(&logi->dm_device_list, &manager->device_lh);
	}

	return DRIVER_OK;
}

DriverResult delete_logical_device(LogicalDevice *logical_device) {
	DeviceOps	*ops   = logical_device->ops;
	DeviceState	 state = logical_device->state;
	DriverResult ret   = DRIVER_OK;

	if (ops->stop && state == DEVICE_STATE_ACTIVE)
		DRIVER_RESULT_PASS(ops->stop(logical_device));
	if (ops->destroy && state != DEVICE_STATE_UNINIT)
		DRIVER_RESULT_PASS(ops->destroy(logical_device));

	if (logical_device->object != NULL) {
		ObjectResult result = delete_object(logical_device->object);
		if (result != OBJECT_OK) ret = DRIVER_ERROR_OBJECT;
	}

	int result = 0;
	if (!list_empty(&logical_device->dm_device_list))
		list_del(&logical_device->dm_device_list);

	list_del(&logical_device->logical_device_list);
	result = kfree(logical_device);
	if (result < 0) ret = DRIVER_ERROR_MEMORY_FREE;

	return ret;
}

DriverResult init_physical_device(PhysicalDevice *device) {
	DEV_OPS_CALL(device, init);
	device->state = DEVICE_STATE_READY;
	return DRIVER_OK;
}

DriverResult start_physical_device(PhysicalDevice *device) {
	DEV_OPS_CALL(device, start);
	device->state = DEVICE_STATE_ACTIVE;
	return DRIVER_OK;
}

DriverResult init_logical_device(LogicalDevice *device) {
	DeviceManager *manager = device_managers[device->type];
	DEV_OPS_CALL(device, init);
	if (manager->ops->init_device_hook != NULL)
		manager->ops->init_device_hook(manager, device);
	device->state = DEVICE_STATE_READY;
	return DRIVER_OK;
}

DriverResult start_logical_device(LogicalDevice *device) {
	DeviceManager *manager = device_managers[device->type];
	DEV_OPS_CALL(device, start);
	if (manager->ops->start_device_hook != NULL)
		manager->ops->start_device_hook(manager, device);
	device->state = DEVICE_STATE_ACTIVE;
	return DRIVER_OK;
}
