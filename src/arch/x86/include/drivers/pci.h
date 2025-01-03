#ifndef _PCI_H
#define _PCI_H

#include "kernel/driver_interface.h"
#include "stdint.h"

#define PCI_CONFIG_ADDR 0xcf8
#define PCI_CONFIG_DATA 0xcfc

#define PCI_REG_DEVICEID   0x00
#define PCI_REG_VENDORID   0x02
#define PCI_REG_STATUS	   0x04
#define PCI_REG_COMMAND	   0x06
#define PCI_REG_CLASSCODE  0x08
#define PCI_REG_SUBCLASS   0x09
#define PCI_REG_PROGIF	   0x0a
#define PCI_REG_REVISIONID 0x0b
#define PCI_REG_BIST	   0x0c
#define PCI_REG_HEADERTYPE 0x0d
#define PCI_REG_LTIMER	   0x0e
#define PCI_REG_CLSIZE	   0x0f

#define PCI_BAR_BASE	 0x10
#define PCI_BAR(x)		 PCI_BAR_BASE + x * 4
#define PCI_BAR_MEM_MASK ~0x0f
#define PCI_BAR_IO_MASK	 ~0x03

#define PCI_MAX_BAR	   6
#define PCI_MAX_BUS	   256
#define PCI_MAX_DEV	   32
#define PCI_MAX_FUNC   8
#define PCI_MAX_DEVICE 256

#define PCI_SEL_REG(bus, device, function, offset)                          \
	{                                                                       \
		uint32_t addr_reg;                                                  \
		addr_reg = (1 << 31) | (bus << 16) | (device << 11) | (func << 8) | \
				   (offset & 0xfc);                                         \
		io_out_dword(PCI_CONFIG_ADDR, addr_reg);                            \
	}

#define DEF_PCI_RW(size)                                             \
	uint##size##_t pci_read##size(                                   \
		uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) { \
		PCI_SEL_REG(bus, device, func, offset);                      \
		return io_in##size(PCI_CONFIG_DATA);                         \
	}                                                                \
	void pci_write##size(                                            \
		uint8_t bus, uint8_t device, uint8_t func, uint8_t offset,   \
		uint##size##_t value) {                                      \
		PCI_SEL_REG(bus, device, func, offset);                      \
		io_out##size(PCI_CONFIG_DATA, value);                        \
	}

#define DEF_PCI_RW_DEVICE(size)                                               \
	uint##size##_t pci_device_read##size(PciDevice *device, uint8_t offset) { \
		return pci_read##size(                                                \
			device->bus_num, device->dev_num, device->function_num, offset);  \
	}                                                                         \
	void pci_device_write##size(                                              \
		PciDevice *device, uint8_t offset, uint##size##_t value) {            \
		pci_write##size(                                                      \
			device->bus_num, device->dev_num, device->function_num, offset,   \
			value);                                                           \
	}

#define PCI_DEVICE_STATUS_INVALID 0
#define PCI_DEVICE_STATUS_UNUSED  1
#define PCI_DEVICE_STATUS_USING	  2
#define PCI_BAR_TYPE_INVALID	  0
#define PCI_BAR_TYPE_MEM		  1
#define PCI_BAR_TYPE_IO			  2

uint8_t	 pci_read8(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
void	 pci_write8(
		uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint8_t value);
void pci_write16(
	uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint16_t value);
void pci_write32(
	uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);

void init_pci();

uint32_t pci_get_device_connected(void);

#include "kernel/bus_driver.h"
#include "kernel/driver.h"

typedef struct PciDeviceBar {
	uint32_t type;
	uint32_t base_addr;
	uint32_t length;
} PciDeviceBar;

typedef struct PciDevice {
	Bus	   *bus;
	Device *device;

	struct PciDriver *pci_driver;

	char status;

	uint8_t bus_num;
	uint8_t dev_num;
	uint8_t function_num;

	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t	 classcode;
	uint8_t	 subclass;
	uint8_t	 prog_if;
	uint8_t	 revision_id;
	uint8_t	 bist;
	uint8_t	 multifunction;
	uint8_t	 header_type;
	uint8_t	 latency_timer;
	uint8_t	 cache_line_size;
	uint8_t	 irqline;
	uint8_t	 irqpin;

	union {
		struct {
			PciDeviceBar bar[PCI_MAX_BAR];

			uint32_t cardbus_cis_pointer;
			uint16_t subsystem_id;
			uint16_t subsystem_vendor_id;
			uint32_t expension_rom_base_address;
			uint8_t	 capabilities_pointer;
			uint8_t	 max_latency;
			uint8_t	 min_grant;
		} common;
		struct {
			PciDeviceBar bar[2];

			uint8_t	 secondary_latency_timer;
			uint8_t	 secondary_bus_number;
			uint8_t	 subordinate_bus_number;
			uint8_t	 primary_bus_number;
			uint16_t secondary_status;

			uint16_t io_base_upper;
			uint16_t io_base;
			uint16_t io_limit_upper;
			uint16_t io_limit;

			uint16_t memory_limit;
			uint16_t memory_base;

			uint32_t prefetchable_limit_upper;
			uint16_t prefetchable_memory_limit;
			uint32_t prefetchable_base_upper;
			uint16_t prefetchable_memory_base;

			uint8_t	 capabilities_pointer;
			uint32_t expension_rom_base_address;
			uint16_t bridge_control;
		} pci2pci_bridge;
		struct {
			uint32_t cardbus_socket_base_address;
			uint16	 secondary_status;
			uint8_t	 capabilities_offset;
			uint8_t	 cardbus_latency_timer;
			uint8_t	 subordiante_bus_number;
			uint8_t	 cardbus_bus_number;
			uint8_t	 pci_bus_number;
			uint32_t memory_base0;
			uint32_t memory_limit0;
			uint32_t memory_base1;
			uint32_t memory_limit1;
			uint32_t io_base0;
			uint32_t io_limit0;
			uint32_t io_base1;
			uint32_t io_limit1;
			uint16_t bridge_control;
			uint16_t subsystem_vendor_id;
			uint16_t subsystem_device_id;
			uint32_t legacy_base_address;
		} pci2cardbus_bridge;
	};
} PciDevice;

typedef struct PciDriverOps {

	DriverResult (*probe)(struct PciDevice *pci_device);
} PciDriverOps;

typedef struct PciDriver {
	list_t pci_driver_list;

	Driver		 *driver;
	DeviceDriver *device_driver;

	enum {
		FIND_BY_CLASSCODE_SUBCLASS,
		FIND_BY_CLASSCODE_SUBCLASS_PROGIF,
		FIND_BY_VENDORID_DEVICEID,
	} find_type;

	union {
		struct {
			uint16_t vendor_id;
			uint16_t device_id;
		} vendor_device;
		struct {
			uint8_t classcode;
			uint8_t subclass;
		} class_subclass;
		struct {
			uint8_t classcode;
			uint8_t subclass;
			uint8_t progif;
		} class_subclass_progif;
	};
	PciDevice *pci_device;

	PciDriverOps *ops;
} PciDriver;

extern list_t pci_driver_lh;
extern Driver pci_driver;

DriverResult pci_set_driver(PciDriver *pci_driver, PciDevice *pci_device);
PciDevice	*pci_alloc_device(void);
int			 pci_free_device(PciDevice *dev);
void		 fill_pci_device_info(
			PciDevice *dev, uint8_t bus, uint8_t device, uint8_t func,
			uint16_t vendorID, uint16_t deviceID, uint32_t classcode,
			uint8_t revisionID, uint8_t multifunction, uint8_t header_type,
			uint8_t bist, uint8_t latency_timer, uint8_t cache_line_size);
void		 get_pci_bar_info(PciDeviceBar *bar, uint32_t addr, uint32_t len);
DriverResult pci_scan_device(
	Bus *bus, uint8_t bus_num, uint8_t device_num, uint8_t function_num,
	PciDevice **out_pci_device);

uint8_t	 pci_device_read8(PciDevice *device, uint8_t offset);
uint16_t pci_device_read16(PciDevice *device, uint8_t offset);
uint32_t pci_device_read32(PciDevice *device, uint8_t offset);
void	 pci_device_write8(PciDevice *device, uint8_t offset, uint8_t value);
void	 pci_device_write16(PciDevice *device, uint8_t offset, uint16_t value);
void	 pci_device_write32(PciDevice *device, uint8_t offset, uint32_t value);

DriverResult pci_register_driver(Driver *driver, PciDriver *new_pci_driver);
uint32_t	 pci_device_get_mem_addr(PciDevice *pci_device);
void		 pci_enable_mem_space(PciDevice *pci_device);
void		 pci_enable_io_space(PciDevice *pci_device);
void		 pci_enable_bus_mastering(PciDevice *pci_device);
uint32_t	 pci_device_get_io_addr(PciDevice *pci_device);

#endif