/**
 * @file pci.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2020-07
 */
#include "kernel/list.h"
#include <driver/bus_dm.h>
#include <drivers/pci.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

LIST_HEAD(pci_driver_lh);

PciDevice pci_devices[PCI_MAX_DEVICE];

DriverResult pci_driver_init(Driver *driver);
DriverResult pci_device_init(Device *device);
DriverResult pci_scan_bus(BusDriver *bus_driver, Bus *bus);
DriverResult pci_init_bus(BusDriver *bus_driver);
DriverResult pci_probe(BusDriver *bus_driver, Bus *bus);

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
	.init				 = pci_init_bus,
};
BusOps pci_bus_ops = {
	.register_device_hook	= NULL,
	.unregister_device_hook = NULL,
	.scan_bus				= pci_scan_bus,
	.probe_device			= pci_probe,
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
	.init			  = pci_driver_init,
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

DEF_PCI_RW(8)
DEF_PCI_RW(16)
DEF_PCI_RW(32)
DEF_PCI_RW_DEVICE(8)
DEF_PCI_RW_DEVICE(16)
DEF_PCI_RW_DEVICE(32)

void fill_pci_device_info(
	PciDevice *dev, uint8_t bus, uint8_t device, uint8_t func,
	uint16_t vendorID, uint16_t deviceID, uint32_t classcode,
	uint8_t revisionID, uint8_t multifunction, uint8_t header_type,
	uint8_t bist, uint8_t latency_timer, uint8_t cache_line_size) {
	int i;

	dev->bus_num		 = bus;
	dev->dev_num		 = device;
	dev->function_num	 = func;
	dev->vendor_id		 = vendorID;
	dev->device_id		 = deviceID;
	dev->classcode		 = classcode >> 16;
	dev->subclass		 = (classcode & 0xff00) >> 8;
	dev->prog_if		 = classcode & 0xff;
	dev->revision_id	 = revisionID;
	dev->multifunction	 = multifunction;
	dev->header_type	 = header_type;
	dev->bist			 = bist;
	dev->latency_timer	 = latency_timer;
	dev->cache_line_size = cache_line_size;

	for (i = 0; i < PCI_MAX_BAR; i++) {
		dev->common.bar[i].type = PCI_BAR_TYPE_INVALID;
	}
	dev->irqline = -1;
}
void pci_enable_bus_mastering(PciDevice *pci_device) {
	uint32_t value = pci_read32(
		pci_device->bus_num, pci_device->dev_num, pci_device->function_num,
		0x04);
	value |= 4;
	pci_write32(
		pci_device->bus_num, pci_device->dev_num, pci_device->function_num,
		0x04, value);
}

void pci_enable_io_space(PciDevice *pci_device) {
	uint32_t value = pci_read32(
		pci_device->bus_num, pci_device->dev_num, pci_device->function_num,
		0x04);
	value |= 1;
	pci_write32(
		pci_device->bus_num, pci_device->dev_num, pci_device->function_num,
		0x04, value);
}

void pci_enable_mem_space(PciDevice *pci_device) {
	uint32_t value = pci_read32(
		pci_device->bus_num, pci_device->dev_num, pci_device->function_num,
		0x04);
	value |= 2;
	pci_write32(
		pci_device->bus_num, pci_device->dev_num, pci_device->function_num,
		0x04, value);
}

uint32_t pci_device_get_mem_addr(PciDevice *pci_device) {
	int i;

	if (pci_device->header_type == 0x0) {
		for (i = 0; i < PCI_MAX_BAR; i++) {
			if (pci_device->common.bar[i].type == PCI_BAR_TYPE_MEM) {
				return pci_device->common.bar[i].base_addr;
			}
		}
	} else if (pci_device->header_type == 0x1) {
		for (i = 0; i < 2; i++) {
			if (pci_device->pci2pci_bridge.bar[i].type == PCI_BAR_TYPE_MEM) {
				return pci_device->pci2pci_bridge.bar[i].base_addr;
			}
		}
	}
	return -1;
}

uint32_t pci_device_get_io_addr(PciDevice *pci_device) {
	int i;

	if (pci_device->header_type == 0x0) {
		for (i = 0; i < PCI_MAX_BAR; i++) {
			if (pci_device->common.bar[i].type == PCI_BAR_TYPE_IO) {
				return pci_device->common.bar[i].base_addr;
			}
		}
	} else if (pci_device->header_type == 0x1) {
		for (i = 0; i < 2; i++) {
			if (pci_device->pci2pci_bridge.bar[i].type == PCI_BAR_TYPE_IO) {
				return pci_device->pci2pci_bridge.bar[i].base_addr;
			}
		}
	}
	return -1;
}

DriverResult pci_scan_bus(BusDriver *bus_driver, Bus *bus) {
	int		   i, j;
	PciDevice *pci_device;
	print_driver_info(
		pci_driver, "device id\tvendor id\theader "
					"type\tclasscode\tsubclass\tprogif\trevision id\n");
	for (i = 0; i < PCI_MAX_DEV; i++) {
		for (j = 0; j < PCI_MAX_FUNC; j++) {
			DriverResult result =
				pci_scan_device(bus, bus->bus_num, i, j, &pci_device);
			if (result == DRIVER_RESULT_DEVICE_NOT_EXIST) {
				continue;
			} else if (result == DRIVER_RESULT_NULL_POINTER) {
				print_error_with_position(
					"pci_probe_bus: pci device(%d:%d:%d) alloc failed!\n",
					bus->bus_num, i, j);
				continue;
			} else if (result == DRIVER_RESULT_UNSUPPORT_DEVICE) {
				print_error_with_position(
					"pci_probe_bus: pci device(%d:%d:%d) unsupport! Header "
					"Type:%d\n",
					bus->bus_num, i, j, pci_device->header_type);
				continue;
			}

			bus_driver->device_count++;
			pci_device->status = PCI_DEVICE_STATUS_UNUSED;
			print_driver_info(
				pci_driver,
				"%#06x\t\t%#06x\t\t%#04x\t\t%#04x\t\t%#04x\t\t%#04x\t%#04x\n",
				pci_device->device_id, pci_device->vendor_id,
				pci_device->header_type, pci_device->classcode,
				pci_device->subclass, pci_device->prog_if,
				pci_device->revision_id);
			if (!pci_device->multifunction) {
				// 没有多个功能就枚举下一个设备
				break;
			}

			if (pci_device->header_type == 1) { // 为PCI-to-PCI桥
				bus_driver->bus_count =
					MAX(bus_driver->bus_count,
						pci_device->pci2pci_bridge.subordinate_bus_number);

				Bus *new_bus		 = kmalloc(sizeof(Bus));
				new_bus->ops		 = &pci_bus_ops;
				new_bus->primary_bus = bus;
				new_bus->bus_num =
					pci_device->pci2pci_bridge.secondary_bus_number;
				new_bus->subordinate_bus_num =
					pci_device->pci2pci_bridge.subordinate_bus_number;

				register_bus(bus_driver, bus->controller_device, new_bus);
			}
		}
	}
	return DRIVER_RESULT_OK;
}

DriverResult pci_init_bus(BusDriver *bus_driver) {
	pci_bus_driver.bus_count = 1; // 默认只有一个主总线

	Bus *bus				 = kmalloc(sizeof(Bus));
	bus->ops				 = &pci_bus_ops;
	bus->bus_num			 = 0;
	bus->subordinate_bus_num = 0;
	bus->primary_bus		 = NULL;
	register_bus(&pci_bus_driver, &pci_device, bus);
	// printk("device id\tvendor id\theader "
	// 	   "type\tclasscode\tsubclass\tprogif\trevision id\n");
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
	Bus *bus, uint8_t bus_num, uint8_t device_num, uint8_t function_num,
	PciDevice **out_pci_device) {
	uint32_t value	  = pci_read32(bus_num, device_num, function_num, 0);
	uint16_t vendorID = value & 0xffff;
	uint16_t deviceID = value >> 16;
	if (vendorID == 0xffff) { return DRIVER_RESULT_DEVICE_NOT_EXIST; }

	value				  = pci_read32(bus_num, device_num, function_num, 0x0c);
	uint8_t bist		  = value >> 24;
	uint8_t multifunction = (value >> 23) & 0x01;
	uint8_t header_type	  = (value >> 16) & 0x7f;
	uint8_t latency_timer = value >> 8;
	uint8_t cache_line_size = value & 0xff;

	value				= pci_read32(bus_num, device_num, function_num, 8);
	uint32_t classcode	= value >> 8;
	uint8_t	 revisionID = value & 0xff;

	PciDevice *pci_device = pci_alloc_device();
	if (pci_device == NULL) { return DRIVER_RESULT_NULL_POINTER; }
	*out_pci_device	   = pci_device;
	pci_device->device = NULL;
	fill_pci_device_info(
		pci_device, bus_num, device_num, function_num, vendorID, deviceID,
		classcode, revisionID, multifunction, header_type, bist, latency_timer,
		cache_line_size);

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
				get_pci_bar_info(&pci_device->common.bar[bar], value, len);
			}
		}

		value = pci_read32(bus_num, device_num, function_num, 0x28);
		pci_device->common.cardbus_cis_pointer = value;

		value = pci_read32(bus_num, device_num, function_num, 0x2c);
		pci_device->common.subsystem_id		   = value >> 16;
		pci_device->common.subsystem_vendor_id = value & 0xffff;

		value = pci_read32(bus_num, device_num, function_num, 0x30);
		pci_device->common.expension_rom_base_address = value;

		value = pci_read32(bus_num, device_num, function_num, 0x34);
		pci_device->common.capabilities_pointer = value & 0xff;

		value = pci_read32(bus_num, device_num, function_num, 0x3c);
		pci_device->common.max_latency = value >> 24;
		pci_device->common.min_grant   = value >> 16;
	} else if (header_type == 0x01) {
		int bar;
		for (bar = 0; bar < 2; bar++) {
			value = pci_read32(bus_num, device_num, function_num, PCI_BAR(bar));
			pci_write32(
				bus_num, device_num, function_num, PCI_BAR(bar), 0xffffffff);
			uint32_t len =
				pci_read32(bus_num, device_num, function_num, PCI_BAR(bar));
			pci_write32(bus_num, device_num, function_num, PCI_BAR(bar), value);

			if (len != 0 && len != 0xffffffff) {
				get_pci_bar_info(&pci_device->common.bar[bar], value, len);
			}
		}

		value = pci_read32(bus_num, device_num, function_num, 0x18);
		pci_device->pci2pci_bridge.secondary_latency_timer = value >> 24;
		pci_device->pci2pci_bridge.subordinate_bus_number  = value >> 16;
		pci_device->pci2pci_bridge.secondary_bus_number	   = value >> 8;
		pci_device->pci2pci_bridge.primary_bus_number	   = value & 0xff;

		value = pci_read32(bus_num, device_num, function_num, 0x1c);
		pci_device->pci2pci_bridge.secondary_status = value >> 16;
		pci_device->pci2pci_bridge.io_limit			= value >> 8;
		pci_device->pci2pci_bridge.io_base			= value & 0xff;

		value = pci_read32(bus_num, device_num, function_num, 0x20);
		pci_device->pci2pci_bridge.memory_limit = value >> 16;
		pci_device->pci2pci_bridge.memory_base	= value & 0xffff;

		value = pci_read32(bus_num, device_num, function_num, 0x24);
		pci_device->pci2pci_bridge.prefetchable_memory_limit = value >> 16;
		pci_device->pci2pci_bridge.prefetchable_memory_base	 = value & 0xffff;

		value = pci_read32(bus_num, device_num, function_num, 0x28);
		pci_device->pci2pci_bridge.prefetchable_base_upper = value;
		value = pci_read32(bus_num, device_num, function_num, 0x2c);
		pci_device->pci2pci_bridge.prefetchable_limit_upper = value;

		value = pci_read32(bus_num, device_num, function_num, 0x30);
		pci_device->pci2pci_bridge.io_limit_upper = value >> 16;
		pci_device->pci2pci_bridge.io_base_upper  = value & 0xffff;

		value = pci_read32(bus_num, device_num, function_num, 0x34);
		pci_device->pci2pci_bridge.capabilities_pointer = value & 0xff;

		value = pci_read32(bus_num, device_num, function_num, 0x38);
		pci_device->pci2pci_bridge.expension_rom_base_address = value;

		value = pci_read32(bus_num, device_num, function_num, 0x3c);
		pci_device->pci2pci_bridge.bridge_control = value >> 16;
	} else if (header_type == 0x02) {
		value = pci_read32(bus_num, device_num, function_num, 0x10);
		pci_device->pci2cardbus_bridge.cardbus_socket_base_address = value;

		value = pci_read32(bus_num, device_num, function_num, 0x14);
		pci_device->pci2cardbus_bridge.secondary_status	   = value >> 16;
		pci_device->pci2cardbus_bridge.capabilities_offset = value & 0xff;

		value = pci_read32(bus_num, device_num, function_num, 0x18);
		pci_device->pci2cardbus_bridge.cardbus_latency_timer  = value >> 24;
		pci_device->pci2cardbus_bridge.subordiante_bus_number = value >> 16;
		pci_device->pci2cardbus_bridge.cardbus_bus_number	  = value >> 8;
		pci_device->pci2cardbus_bridge.pci_bus_number		  = value & 0xff;

		value = pci_read32(bus_num, device_num, function_num, 0x1c);
		pci_device->pci2cardbus_bridge.memory_base0 = value;
		value = pci_read32(bus_num, device_num, function_num, 0x20);
		pci_device->pci2cardbus_bridge.memory_limit0 = value;

		value = pci_read32(bus_num, device_num, function_num, 0x24);
		pci_device->pci2cardbus_bridge.memory_base1 = value;
		value = pci_read32(bus_num, device_num, function_num, 0x28);
		pci_device->pci2cardbus_bridge.memory_limit1 = value;

		value = pci_read32(bus_num, device_num, function_num, 0x2c);
		pci_device->pci2cardbus_bridge.io_base0 = value;
		value = pci_read32(bus_num, device_num, function_num, 0x30);
		pci_device->pci2cardbus_bridge.io_limit0 = value;

		value = pci_read32(bus_num, device_num, function_num, 0x34);
		pci_device->pci2cardbus_bridge.io_base1 = value;
		value = pci_read32(bus_num, device_num, function_num, 0x38);
		pci_device->pci2cardbus_bridge.io_limit1 = value;

		value = pci_read32(bus_num, device_num, function_num, 0x40);
		pci_device->pci2cardbus_bridge.subsystem_vendor_id = value >> 16;
		pci_device->pci2cardbus_bridge.subsystem_device_id = value & 0xffff;

		value = pci_read32(bus_num, device_num, function_num, 0x44);
		pci_device->pci2cardbus_bridge.legacy_base_address = value;

		value = pci_read32(bus_num, device_num, function_num, 0x3c);
		pci_device->pci2cardbus_bridge.bridge_control = value >> 16;
	} else {
		print_error_with_position(
			"unsupport PCI Header Type: %d\n", header_type);
		return DRIVER_RESULT_UNSUPPORT_DEVICE;
	}
	pci_device->irqline = value & 0xff;
	pci_device->irqpin	= value >> 8;

	PciDriver *pci_driver;
	list_for_each_owner (pci_driver, &pci_driver_lh, pci_driver_list) {
		if (pci_driver->pci_device != NULL) { continue; }
		if (pci_driver->find_type == FIND_BY_VENDORID_DEVICEID) {
			if (pci_driver->vendor_device.vendor_id == vendorID &&
				pci_driver->vendor_device.device_id == deviceID) {
				pci_device->pci_driver = pci_driver;
				pci_driver->pci_device = pci_device;
				break;
			}
		} else if (pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS) {
			if (pci_driver->class_subclass.classcode == classcode &&
				pci_driver->class_subclass.subclass == pci_device->subclass) {
				pci_device->pci_driver = pci_driver;
				pci_driver->pci_device = pci_device;
				break;
			}
		} else if (pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS_PROGIF) {
			if (pci_driver->class_subclass_progif.classcode == classcode &&
				pci_driver->class_subclass_progif.subclass ==
					pci_device->subclass &&
				pci_driver->class_subclass_progif.progif ==
					pci_device->prog_if) {
				pci_device->pci_driver = pci_driver;
				pci_driver->pci_device = pci_device;
				break;
			}
		}
	}

	return DRIVER_RESULT_OK;
}

bool pci_match_driver(PciDriver *pci_driver, PciDriver *new_pci_driver) {
	if (new_pci_driver->find_type == FIND_BY_VENDORID_DEVICEID) {
		if (new_pci_driver->vendor_device.vendor_id ==
				pci_driver->vendor_device.vendor_id &&
			new_pci_driver->vendor_device.device_id ==
				pci_driver->vendor_device.device_id) {
			return true;
		}
	} else if (new_pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS) {
		if (new_pci_driver->class_subclass.classcode ==
				pci_driver->class_subclass.classcode &&
			new_pci_driver->class_subclass.subclass ==
				pci_driver->class_subclass.subclass) {
			return true;
		}
	} else if (new_pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS_PROGIF) {
		if (new_pci_driver->class_subclass_progif.classcode ==
				pci_driver->class_subclass_progif.classcode &&
			new_pci_driver->class_subclass_progif.subclass ==
				pci_driver->class_subclass_progif.subclass &&
			new_pci_driver->class_subclass_progif.progif ==
				pci_driver->class_subclass_progif.progif) {
			return true;
		}
	}
	return false;
}

bool pci_match_device(PciDriver *pci_driver, PciDevice *pci_device) {
	if (pci_driver->find_type == FIND_BY_VENDORID_DEVICEID) {
		if (pci_driver->vendor_device.vendor_id == pci_device->vendor_id &&
			pci_driver->vendor_device.device_id == pci_device->device_id) {
			return true;
		}
	} else if (pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS) {
		if (pci_driver->class_subclass.classcode == pci_device->classcode &&
			pci_driver->class_subclass.subclass == pci_device->subclass) {
			return true;
		}
	} else if (pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS_PROGIF) {
		if (pci_driver->class_subclass_progif.classcode ==
				pci_device->classcode &&
			pci_driver->class_subclass_progif.subclass ==
				pci_device->subclass &&
			pci_driver->class_subclass_progif.progif == pci_device->prog_if) {
			return true;
		}
	}
	return false;
}

DriverResult pci_register_driver(Driver *driver, PciDriver *new_pci_driver) {
	PciDriver *old_pci_driver;
	list_for_each_owner (old_pci_driver, &pci_driver_lh, pci_driver_list) {
		if (new_pci_driver->find_type == old_pci_driver->find_type) {
			if (pci_match_driver(old_pci_driver, new_pci_driver)) {
				return DRIVER_RESULT_DEVICE_DRIVER_CONFLICT;
			}
			break;
		} else if (
			new_pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS_PROGIF &&
			old_pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS) {
			if (new_pci_driver->class_subclass_progif.classcode ==
					old_pci_driver->class_subclass_progif.classcode &&
				new_pci_driver->class_subclass_progif.subclass ==
					old_pci_driver->class_subclass_progif.subclass &&
				new_pci_driver->class_subclass_progif.progif ==
					old_pci_driver->class_subclass_progif.progif) {
				list_del(&old_pci_driver->pci_driver_list);
				break;
			}
		} else if (
			new_pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS &&
			old_pci_driver->find_type == FIND_BY_CLASSCODE_SUBCLASS_PROGIF) {
			if (new_pci_driver->class_subclass.classcode ==
					old_pci_driver->class_subclass.classcode &&
				new_pci_driver->class_subclass.subclass ==
					old_pci_driver->class_subclass.subclass) {
				return DRIVER_RESULT_DEVICE_DRIVER_CONFLICT;
			}
		}
	}

	list_add_tail(&new_pci_driver->pci_driver_list, &pci_driver_lh);
	return DRIVER_RESULT_OK;
}

DriverResult pci_device_init(Device *device) {
	int i;
	for (i = 0; i < PCI_MAX_DEVICE; i++) {
		pci_devices[i].status = PCI_DEVICE_STATUS_INVALID;
	}
	return DRIVER_RESULT_OK;
}

DriverResult pci_probe(BusDriver *bus_driver, Bus *bus) {
	PciDriver *pci_driver;
	for (int i = 0; i < PCI_MAX_DEVICE; i++) {
		if (pci_devices[i].status == PCI_DEVICE_STATUS_UNUSED) {
			list_for_each_owner (pci_driver, &pci_driver_lh, pci_driver_list) {
				if (pci_match_device(pci_driver, &pci_devices[i])) {
					pci_devices[i].status	  = PCI_DEVICE_STATUS_USING;
					pci_devices[i].pci_driver = pci_driver;
					pci_devices[i].bus		  = bus;
					pci_driver->pci_device	  = &pci_devices[i];
					pci_driver->ops->probe(&pci_devices[i]);
				}
			}
		}
	}
	return DRIVER_RESULT_OK;
}

DriverResult pci_driver_init(Driver *driver) {
	check_dependency(&pci_driver);
	pci_device_driver.bus = pci_dependencies[0].out_bus;
	DRV_RESULT_DELIVER_CALL(
		register_bus_controller_device, &pci_device_driver, &pci_bus_driver,
		&pci_device, &pci_bus_controller_device);
	return DRIVER_RESULT_OK;
}

static __init void pci_initcall(void) {
	register_driver(&pci_driver);
	register_device_driver(&pci_driver, &pci_device_driver);
	register_bus_driver(&pci_driver, &pci_bus_driver);
}

driver_initcall(pci_initcall);
