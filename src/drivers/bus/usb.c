#include "drivers/bus/usb.h"
#include "kernel/device.h"
#include "kernel/device_driver.h"
#include "kernel/driver.h"
#include "kernel/list.h"
#include "objects/object.h"
#include <driver/bus_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>
#include <kernel/platform.h>

LIST_HEAD(hci_lh);

BusDriverOps usb_bus_driver_ops = {
	.register_bus_hook	 = NULL,
	.unregister_bus_hook = NULL,
	.init				 = NULL,
};
DeviceDriverOps usb_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
BusOps usb_bus_ops = {
	.register_device_hook	= NULL,
	.unregister_device_hook = NULL,
	.scan_bus				= NULL,
	.probe_device			= NULL,
};
DeviceOps usb_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};

DriverDependency usb_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_PCI, 0},
		.out_bus		   = NULL,
	 },
};
Driver usb_driver = {
	.short_name		  = STRING_INIT("UsbDriver"),
	.dependency_count = sizeof(usb_dependencies) / sizeof(DriverDependency),
	.dependencies	  = usb_dependencies,
	.init			  = NULL,
};
BusDriver usb_bus_driver = {
	.name			   = STRING_INIT("USB"),
	.driver_type	   = DRIVER_TYPE_BUS_DRIVER,
	.bus_type		   = BUS_TYPE_USB,
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &usb_bus_driver_ops,
};
DeviceDriver usb_device_driver = {
	.name			   = STRING_INIT("USB Device Driver"),
	.type			   = DEVICE_TYPE_BUS_CONTROLLER,
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &usb_device_driver_ops,
};
Device usb_device = {
	.name			   = STRING_INIT("System USB Controller"),
	.state			   = DEVICE_STATE_UNREGISTERED,
	.bus			   = &platform_bus,
	.private_data_size = 0,
	.ops			   = &usb_device_ops,
};
BusControllerDevice usb_bus_controller_device = {
	.short_name			= STRING_INIT("USB Bus Controller"),
	.device				= &usb_device,
	.bus_driver			= &usb_bus_driver,
	.bus_controller_ops = NULL,
};

static __init void usb_bus_driver_entry(void) {
	ObjectAttr attr = device_object_attr;
	register_driver(&usb_driver);
	register_bus_driver(&usb_driver, &usb_bus_driver, &attr);

	HciInit *hci_init;
	list_for_each_owner (hci_init, &hci_lh, list) {
		if (hci_init->init) hci_init->init(&usb_driver);
	}
}

driver_initcall(usb_bus_driver_entry);
