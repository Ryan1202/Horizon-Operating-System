#include "kernel/initcall.h"
#include <driver/bus_dm.h>
#include <drivers/bus/isa/dma.h>
#include <drivers/bus/isa/isa.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/platform.h>

LIST_HEAD(isa_driver_lh);

DriverResult isa_driver_init(Driver *driver);
DriverResult isa_probe(BusDriver *bus_driver, Bus *bus);
DriverResult isa_init_bus(BusDriver *bus_driver);

BusOps isa_bus_ops = {
	.scan_bus	  = NULL,
	.probe_device = isa_probe,
};

DriverDependency isa_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_PLATFORM, 0},
		.out_bus		   = NULL,
	 },
};
Driver isa_driver = {
	.short_name = STRING_INIT("IsaDriver"),
};
DeviceDriver isa_device_driver;
BusDriver	 isa_bus_driver = {
	   .name = STRING_INIT("ISA"),
};
Bus *isa_bus;

DriverResult isa_register_device_driver(
	DeviceDriver *device_driver, IsaOps *ops) {
	IsaDeviceDriver *isa_device_driver = kmalloc(sizeof(IsaDeviceDriver));
	if (isa_device_driver == NULL) { return DRIVER_ERROR_OUT_OF_MEMORY; }

	isa_device_driver->device_driver = device_driver;
	isa_device_driver->ops			 = ops;
	list_add_tail(&isa_device_driver->list, &isa_driver_lh);

	return DRIVER_OK;
}

DriverResult isa_probe(BusDriver *bus_driver, Bus *bus) {
	IsaDeviceDriver *isa_device_driver;
	list_for_each_owner (isa_device_driver, &isa_driver_lh, list) {
		isa_device_driver->bus_driver = bus_driver;
		isa_device_driver->bus		  = bus;
		isa_device_driver->ops->probe(isa_device_driver);
	}
	return DRIVER_OK;
}

static void __init isa_initcall(void) {
	DriverResult result;
	ObjectAttr	 attr = driver_object_attr;

	result = register_driver(&isa_driver);
	if (result != DRIVER_OK) goto failed_register_driver;

	result = register_device_driver(&isa_driver, &isa_device_driver);
	if (result != DRIVER_OK) goto failed_register_device_driver;

	result =
		register_bus_driver(&isa_driver, BUS_TYPE_ISA, &isa_bus_driver, &attr);
	if (result != DRIVER_OK) goto failed_resgister_bus_driver;

	result = create_bus(&isa_bus, &isa_bus_driver, &isa_bus_ops);
	if (result != DRIVER_OK) goto failed_create_bus;

	return;

failed_create_bus:
	unregister_bus_driver(&isa_bus_driver);

failed_resgister_bus_driver:
	unregister_device_driver(&isa_device_driver);

failed_register_device_driver:
	unregister_driver(&isa_driver);

failed_register_driver:
	return;
}

driver_initcall(isa_initcall);
