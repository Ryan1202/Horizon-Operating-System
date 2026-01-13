#include <drivers/bus/pci/pci.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/initcall.h>
#include <kernel/platform.h>
#include <result.h>
#include <string.h>

#include "include/ata_driver.h"
#include "include/ide_controller.h"

DriverDependency ata_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_PCI, 0},
		.out_bus		   = NULL,
	 },
};

Driver ata_driver = {
	.short_name = STRING_INIT("AtaDriver"),
};

static __init void ata_driver_initcall(void) {
	register_driver(&ata_driver);
	register_device_driver(&ata_driver, &ide_controller_device_driver);
	pci_register_driver(&ata_driver, &ide_pci_driver);
}

driver_initcall(ata_driver_initcall);