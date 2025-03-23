#include "kernel/initcall.h"
#include "objects/object.h"
#include <driver/bus_dm.h>
#include <drivers/bus/isa/dma.h>
#include <drivers/bus/isa/isa.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_manager.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/platform.h>

LIST_HEAD(isa_driver_lh);

DriverResult isa_driver_init(Driver *driver);
DriverResult isa_probe(BusDriver *bus_driver, Bus *bus);
DriverResult isa_init_bus(BusDriver *bus_driver);

DeviceDriverOps isa_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
DeviceOps isa_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};
BusDriverOps isa_bus_driver_ops = {
	.register_bus_hook	 = NULL,
	.unregister_bus_hook = NULL,
	.init				 = isa_init_bus,
};
BusOps isa_bus_ops = {
	.register_device_hook	= NULL,
	.unregister_device_hook = NULL,
	.scan_bus				= NULL,
	.probe_device			= isa_probe,
};
BusControllerDeviceOps isa_controller_ops = {
	.probe = NULL,
};

DriverDependency isa_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_PLATFORM, 0},
		.out_bus		   = NULL,
	 },
};
Driver isa_driver = {
	.short_name		  = STRING_INIT("PciDriver"),
	.dependency_count = sizeof(isa_dependencies) / sizeof(DriverDependency),
	.dependencies	  = isa_dependencies,
	.init			  = isa_driver_init,
};
DeviceDriver isa_device_driver = {
	.name			   = STRING_INIT("ISA Device Driver"),
	.bus			   = NULL,
	.type			   = DEVICE_TYPE_BUS_CONTROLLER,
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &isa_driver_ops,
};
Device isa_device = {
	.name			   = STRING_INIT("ISA Controller"),
	.state			   = DEVICE_STATE_UNREGISTERED,
	.bus			   = &platform_bus,
	.private_data_size = 0,
	.ops			   = &isa_device_ops,
};
BusDriver isa_bus_driver = {
	.name			   = STRING_INIT("ISA"),
	.driver_type	   = DRIVER_TYPE_BUS_DRIVER,
	.bus_type		   = BUS_TYPE_ISA,
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &isa_bus_driver_ops,
};
BusControllerDevice isa_bus_controller_device = {
	.short_name			= STRING_INIT("ISA"),
	.device				= &isa_device,
	.bus_driver			= &isa_bus_driver,
	.bus_controller_ops = &isa_controller_ops,
};
Bus isa_bus = {
	.bus_num		   = 0,
	.bus_driver		   = &isa_bus_driver,
	.controller_device = &isa_device,
	.ops			   = &isa_bus_ops,
};

DriverResult isa_register_device_driver(
	DeviceDriver *device_driver, IsaOps *ops) {
	IsaDeviceDriver *isa_device_driver = kmalloc(sizeof(IsaDeviceDriver));
	if (isa_device_driver == NULL) { return DRIVER_RESULT_OUT_OF_MEMORY; }

	isa_device_driver->device_driver = device_driver;
	isa_device_driver->ops			 = ops;
	list_add_tail(&isa_device_driver->list, &isa_driver_lh);

	return DRIVER_RESULT_OK;
}

DriverResult isa_probe(BusDriver *bus_driver, Bus *bus) {
	IsaDeviceDriver *isa_device_driver;
	list_for_each_owner (isa_device_driver, &isa_driver_lh, list) {
		isa_device_driver->bus_driver = bus_driver;
		isa_device_driver->bus		  = bus;
		isa_device_driver->ops->probe(isa_device_driver);
	}
	return DRIVER_RESULT_OK;
}

DriverResult isa_init_bus(BusDriver *bus_driver) {
	isa_bus_driver.bus_count = 1;

	ObjectAttr attr = device_object_attr;
	register_bus(&isa_bus_driver, &isa_device, &isa_bus, &attr);
	return DRIVER_RESULT_OK;
}

DriverResult isa_driver_init(Driver *driver) {
	isa_device_driver.bus = isa_dependencies[0].out_bus;
	ObjectAttr attr		  = device_object_attr;
	DRIVER_RESULT_PASS(register_bus_controller_device(
		&isa_device_driver, &isa_bus_driver, &isa_device,
		&isa_bus_controller_device, &attr));
	return DRIVER_RESULT_OK;
}

static void __init isa_initcall(void) {
	register_driver(&isa_driver);
	register_device_driver(&isa_driver, &isa_device_driver);
	ObjectAttr attr = driver_object_attr;
	register_bus_driver(&isa_driver, &isa_bus_driver, &attr);
}

driver_initcall(isa_initcall);
