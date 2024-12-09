/**
 * @file pci.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2020-07
 */
#include "kernel/wait_queue.h"
#include <driver/bus_dm.h>
#include <drivers/pci.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <string.h>
#include <types.h>

// struct pci_device pci_devices[PCI_MAX_DEVICE];

// void init_pci() {
// 	int i, j, k;

// 	printk("device id\tvendor id\theader "
// 		   "type\tclasscode\tsubclass\tprogif\trevision id\n");
// 	for (i = 0; i < PCI_MAX_DEVICE; i++) {
// 		pci_devices[i].status = PCI_DEVICE_STATUS_INVALID;
// 	}
// 	for (i = 0; i < PCI_MAX_BUS; i++) {
// 		for (j = 0; j < PCI_MAX_DEV; j++) {
// 			for (k = 0; k < PCI_MAX_FUNC; k++) {
// 				pci_scan_device(i, j, k);
// 			}
// 		}
// 	}
// }

PCI_READ(8, uint8_t)
PCI_READ(16, uint16_t)
PCI_READ(32, uint32_t)
PCI_WRITE(8, uint8_t)
PCI_WRITE(16, uint16_t)
PCI_WRITE(32, uint32_t)
PCI_READ_DEVICE(8, uint8_t)
PCI_READ_DEVICE(16, uint16_t)
PCI_READ_DEVICE(32, uint32_t)
PCI_WRITE_DEVICE(8, uint8_t)
PCI_WRITE_DEVICE(16, uint16_t)
PCI_WRITE_DEVICE(32, uint32_t)

// struct pci_device *pci_get_device_ById(uint16_t vendorID, uint16_t deviceID)
// { 	int				   i; 	struct pci_device *device;

// 	for (i = 0; i < PCI_MAX_DEVICE; i++) {
// 		device = &pci_devices[i];
// 		if (device->vendorID == vendorID && device->deviceID == deviceID) {
// 			return device;
// 		}
// 	}
// 	return NULL;
// }

// struct pci_device *pci_get_device_ByClassFull(
// 	uint8_t classcode, uint8_t subclass, uint8_t progif) {
// 	int				   i;
// 	struct pci_device *device;

// 	for (i = 0; i < PCI_MAX_DEVICE; i++) {
// 		device = &pci_devices[i];
// 		if (device->classcode == classcode && device->subclass == subclass &&
// 			device->prog_if == progif) {
// 			return device;
// 		}
// 	}
// 	return NULL;
// }

// struct pci_device *pci_get_device_ByClass(uint8_t classcode, uint8_t
// subclass) { 	int				   i; 	struct pci_device *device;

// 	for (i = 0; i < PCI_MAX_DEVICE; i++) {
// 		device = &pci_devices[i];
// 		if (device->classcode == classcode && device->subclass == subclass) {
// 			return device;
// 		}
// 	}
// 	return NULL;
// }

void pci_enable_bus_mastering(struct pci_device *device) {
	uint32_t value =
		pci_read32(device->bus, device->dev, device->function, 0x04);
	value |= 4;
	pci_write32(device->bus, device->dev, device->function, 0x04, value);
}

void pci_enable_io_space(struct pci_device *device) {
	uint32_t value =
		pci_read32(device->bus, device->dev, device->function, 0x04);
	value |= 1;
	pci_write32(device->bus, device->dev, device->function, 0x04, value);
}

void pci_enable_mem_space(struct pci_device *device) {
	uint32_t value =
		pci_read32(device->bus, device->dev, device->function, 0x04);
	value |= 2;
	pci_write32(device->bus, device->dev, device->function, 0x04, value);
}

void get_pci_device_info(
	PciDevice *dev, uint8_t bus, uint8_t device, uint8_t func,
	uint16_t vendorID, uint16_t deviceID, uint32_t classcode,
	uint8_t revisionID, uint8_t multifunction) {
	int i;

	dev->bus_num	   = bus;
	dev->dev_num	   = device;
	dev->function_num  = func;
	dev->vendorID	   = vendorID;
	dev->deviceID	   = deviceID;
	dev->classcode	   = classcode >> 16;
	dev->subclass	   = (classcode & 0xff00) >> 8;
	dev->prog_if	   = classcode & 0xff;
	dev->revisionID	   = revisionID;
	dev->multifunction = multifunction;

	for (i = 0; i < PCI_MAX_BAR; i++) {
		dev->bar[i].type = PCI_BAR_TYPE_INVALID;
	}
	dev->irqline = -1;
}

uint32_t pci_device_get_mem_addr(struct pci_device *dev) {
	int i;

	for (i = 0; i < PCI_MAX_BAR; i++) {
		if (dev->bar[i].type == PCI_BAR_TYPE_MEM) {
			return dev->bar[i].base_addr;
		}
	}
	return -1;
}

uint32_t pci_device_get_io_addr(struct pci_device *dev) {
	int i;

	for (i = 0; i < PCI_MAX_BAR; i++) {
		if (dev->bar[i].type == PCI_BAR_TYPE_IO) {
			return dev->bar[i].base_addr;
		}
	}
	return -1;
}

// --------new--------
#include <kernel/bus_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>

PciDevice pci_devices[PCI_MAX_DEVICE];

DriverResult pci_device_init(Device *device);

DeviceDriverOps pci_driver_ops = {
	.register_driver_hook	= NULL,
	.unregister_driver_hook = NULL,
};
DeviceOps pci_device_ops = {
	.init	 = pci_device_init,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};
BusDriverOps pci_bus_driver_ops = {
	.register_bus_hook	 = NULL,
	.unregister_bus_hook = NULL,
};
BusOps pci_bus_ops = {
	.register_device_hook	= NULL,
	.unregister_device_hook = NULL,
};
BusControllerDeviceOps pci_controller_ops = {
	.probe = NULL,
};

DriverDependency pci_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_PLATFORM, 0},
		.out_bus		   = NULL,
	 },
};
Driver pci_driver = {
	.name			  = STRING_INIT("pci driver"),
	.dependency_count = sizeof(pci_dependencies) / sizeof(DriverDependency),
	.dependencies	  = pci_dependencies,
};
DeviceDriver pci_device_driver = {
	.name			   = STRING_INIT("pci device driver"),
	.bus			   = NULL,
	.type			   = DEVICE_TYPE_BUS_CONTROLLER,
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &pci_driver_ops,
};
Device pci_device = {
	.name			   = STRING_INIT("pci controller"),
	.state			   = DEVICE_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &pci_device_ops,
};
BusDriver pci_bus_driver = {
	.name			   = STRING_INIT("pci"),
	.driver_type	   = DRIVER_TYPE_BUS_DRIVER,
	.bus_type		   = BUS_TYPE_PCI,
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &pci_bus_driver_ops,
};
BusControllerDevice pci_bus_controller_device = {
	.device				= &pci_device,
	.bus_driver			= &pci_bus_driver,
	.bus_controller_ops = &pci_controller_ops,
};

DriverResult pci_device_init(Device *device) {
	int i;
	for (i = 0; i < PCI_MAX_DEVICE; i++) {
		pci_devices[i].status = PCI_DEVICE_STATUS_INVALID;
	}
	wait_queue_wakeup_all(&bus_wqm[BUS_TYPE_PCI]);
	return DRIVER_RESULT_OK;
}

DriverResult pci_probe(Device *device) {
	int	 i, j, k;
	bool flag;
	// printk("device id\tvendor id\theader "
	// 	   "type\tclasscode\tsubclass\tprogif\trevision id\n");
	for (i = 0; i < PCI_MAX_BUS; i++) {
		Bus *bus = kmalloc(sizeof(Bus));
		bus->ops = &pci_bus_ops;
		flag	 = false;
		for (j = 0; j < PCI_MAX_DEV; j++) {
			for (k = 0; k < PCI_MAX_FUNC; k++) {
				DriverResult result = pci_scan_device(bus, i, j, k);
				if (result == DRIVER_RESULT_DEVICE_NOT_EXIST) {
					continue;
				} else if (result == DRIVER_RESULT_OK) {
					flag = true;
				} else if (result == DRIVER_RESULT_NULL_POINTER) {
					print_error(
						"pci_probe: pci device(%d:%d:%d) alloc failed!\n", i, j,
						k);
				}
			}
		}
		if (flag) {
			// 如果该总线下有设备则注册总线
			register_bus(&pci_bus_driver, &pci_device, bus);
		} else {
			// 如果没有设备则释放内存
			kfree(bus);
		}
	}
	return DRIVER_RESULT_OK;
}

PciDevice *pci_alloc_device(void) {
	int i;

	for (i = 0; i < PCI_MAX_DEVICE; i++) {
		if (pci_devices[i].status == PCI_DEVICE_STATUS_INVALID) {
			pci_devices[i].status = PCI_DEVICE_STATUS_USING;
			return &pci_devices[i];
		}
	}
	return NULL;
}

uint32_t pci_get_device_connected(void) {
	int		   i;
	PciDevice *device;
	for (i = 0; i < PCI_MAX_BAR; i++) {
		device = &pci_devices[i];
		if (device->status != PCI_DEVICE_STATUS_USING) { break; }
	}
	return i;
}

int pci_free_device(PciDevice *dev) {
	int i;

	for (i = 0; i < PCI_MAX_DEVICE; i++) {
		if (&pci_devices[i] == dev) {
			dev->status = PCI_DEVICE_STATUS_INVALID;
			return 0;
		}
	}
	return -1;
}

void get_pci_bar_info(PciDeviceBar *bar, uint32_t addr, uint32_t len) {
	if (addr == 0xffffffff) { addr = 0; }
	if (addr & 1) // I/O内存
	{
		bar->type	   = PCI_BAR_TYPE_IO;
		bar->base_addr = addr & PCI_BAR_IO_MASK;
		bar->length	   = ~(len & PCI_BAR_IO_MASK) + 1;
	} else {
		bar->type	   = PCI_BAR_TYPE_MEM;
		bar->base_addr = addr & PCI_BAR_MEM_MASK;
		bar->length	   = ~(len & PCI_BAR_MEM_MASK) + 1;
	}
}

DriverResult pci_scan_device(
	Bus *bus, uint8_t bus_num, uint8_t device_num, uint8_t function_num) {
	uint32_t value	  = pci_read32(bus_num, device_num, function_num, 0);
	uint16_t vendorID = value & 0xffff;
	uint16_t deviceID = value >> 16;
	if (vendorID == 0xffff) { return DRIVER_RESULT_DEVICE_NOT_EXIST; }
	value				= pci_read32(bus_num, device_num, function_num, 0x0c);
	uint8_t header_type = value >> 16;
	value				= pci_read32(bus_num, device_num, function_num, 8);
	uint32_t classcode	= value >> 8;
	uint8_t	 revisionID = value & 0xff;

	PciDevice *pci_device = pci_alloc_device();
	if (pci_device == NULL) { return DRIVER_RESULT_NULL_POINTER; }
	pci_device->device = NULL;
	get_pci_device_info(
		pci_device, bus_num, device_num, function_num, vendorID, deviceID,
		classcode, revisionID, header_type);

	if (header_type == 0x00) {
		int bar;
		for (bar = 0; bar < PCI_MAX_BAR; bar++) {
			value = pci_read32(bus_num, device_num, function_num, PCI_BAR(bar));
			pci_write32(
				bus_num, device_num, function_num, PCI_BAR(bar), 0xffffffff);
			uint32_t len =
				pci_read32(bus_num, device_num, function_num, PCI_BAR(bar));
			pci_write32(bus_num, device_num, function_num, PCI_BAR(bar), value);

			if (len != 0 && len != 0xffffffff) {
				get_pci_bar_info(&pci_device->bar[bar], value, len);
			}
		}
	}

	value = pci_read32(bus_num, device_num, function_num, 0x3c) & 0xffff;
	if ((value & 0xff) > 0 && (value & 0xff) < 32) {
		pci_device->irqline = value & 0xff;
		pci_device->irqpin	= value >> 8;
	}

	// printk(
	// 	"%#06x\t\t%#06x\t\t%#04x\t\t%#04x\t\t%#04x\t\t%#04x\t%#04x\n", deviceID,
	// 	vendorID, header_type & (uint8_t)(~0x80), dev->classcode, dev->subclass,
	// 	dev->prog_if, revisionID);
	return DRIVER_RESULT_OK;
}

static __init void pci_driver_entry(void) {
	register_driver(&pci_driver);
	pci_device_driver.bus = pci_dependencies[0].out_bus;
	register_device_driver(&pci_driver, &pci_device_driver);
	register_bus_driver(&pci_driver, &pci_bus_driver);
	register_bus_controller_device(
		&pci_device_driver, &pci_bus_driver, &pci_device,
		&pci_bus_controller_device);
}

driver_initcall(pci_driver_entry);
