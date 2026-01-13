#include <kernel/bus_driver.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/wait_queue.h>
#include <math.h>
#include <objects/object.h>
#include <string.h>

BusDriver *bus_drivers[BUS_TYPE_MAX];
WaitQueue  bus_wqm[BUS_TYPE_MAX];

DriverResult init_bus_manager() {
	for (int i = 0; i < BUS_TYPE_MAX; i++) {
		wait_queue_init(&bus_wqm[i]);
	}
	return DRIVER_OK;
}

DriverResult register_bus_driver(
	Driver *driver, BusType type, BusDriver *bus_driver, ObjectAttr *attr) {
	BusDriver *_bus_driver = bus_drivers[type];
	if (_bus_driver != NULL) return DRIVER_ERROR_ALREADY_EXIST;

	bus_driver->state = DRIVER_STATE_REGISTERED;
	list_init(&bus_driver->bus_lh);

	bus_drivers[type] = bus_driver;

	bus_driver->bus_type	= type;
	bus_driver->new_bus_num = 0;
	bus_driver->bus_count	= 0;

	bus_driver->object =
		create_object_directory(&bus_object, &bus_driver->name, *attr);

	return DRIVER_OK;
}

DriverResult unregister_bus_driver(BusDriver *bus_driver) {
	delete_object(bus_driver->object);

	bus_drivers[bus_driver->bus_type] = NULL;

	Bus *cur, *next;
	list_for_each_owner_safe (cur, next, &bus_driver->bus_lh, bus_list) {
		delete_bus(cur);
	}

	return DRIVER_OK;
}

DriverResult create_bus(Bus **bus, BusDriver *bus_driver, BusOps *ops) {
	*bus = kmalloc(sizeof(Bus));
	if (*bus == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;
	Bus *b = *bus;

	Bus *primary_bus = b->primary_bus;
	Bus *tmp_bus	 = b;
	while (primary_bus != NULL) {
		primary_bus->subordinate_bus_num =
			MAX(primary_bus->subordinate_bus_num, tmp_bus->subordinate_bus_num);
		tmp_bus		= primary_bus;
		primary_bus = primary_bus->primary_bus;
	}
	char	 _name[6] = {0}; // bus_count为uint16_t类型，最大65535，5位数
	string_t name;
	itoa(_name, bus_driver->new_bus_num++, 10);
	bus_driver->bus_count++;
	string_new(&name, _name, sizeof(_name));

	b->bus_driver	  = bus_driver;
	b->ops			  = ops;
	b->new_device_num = 0;
	b->device_count	  = 0;
	list_init(&b->device_lh);
	list_add_tail(&b->bus_list, &bus_driver->bus_lh);

	b->object =
		create_object_directory(bus_driver->object, &name, base_obj_sys_attr);

	list_add_tail(&b->new_bus_list, &new_bus_lh);

	return DRIVER_OK;
}

DriverResult delete_bus(Bus *bus) {
	delete_object(bus->object);

	// 取消注册bus下的所有device_driver
	PhysicalDevice *cur, *next;
	list_for_each_owner_safe (cur, next, &bus->device_lh, device_list) {
		delete_physical_device(cur);
	}

	list_del(&bus->bus_list);
	bus->bus_driver = NULL;

	return DRIVER_OK;
}
