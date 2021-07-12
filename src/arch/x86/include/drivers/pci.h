#ifndef _PCI_H
#define _PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDR		0xcf8
#define PCI_CONFIG_DATA		0xcfc

#define PCI_BAR_BASE		0x10
#define PCI_BAR(x)			PCI_BAR_BASE + x*4
#define PCI_BAR_MEM_MASK	~0x0f
#define PCI_BAR_IO_MASK		~0x02

#define PCI_MAX_BAR			6
#define PCI_MAX_BUS			256
#define PCI_MAX_DEV			32
#define PCI_MAX_FUNC		8
#define PCI_MAX_DEVICE		256

struct pci_device_bar
{
	uint32_t type;
    uint32_t base_addr;
    uint32_t length;
};

#define PCI_DEVICE_STATUS_INVALID 		0
#define PCI_DEVICE_STATUS_USING		 	1
#define PCI_BAR_TYPE_INVALID 			0
#define PCI_BAR_TYPE_MEM 				1
#define PCI_BAR_TYPE_IO 				2

struct pci_device
{
	char status;
	
	uint8_t bus;
	uint8_t dev;
	uint8_t function;
	
	uint16_t vendorID;
    uint16_t deviceID;
    uint8_t classcode;
    uint8_t subclass;
    uint8_t prog_if;
	uint8_t revisionID;
    uint8_t multifunction;
    uint8_t irqline;
    uint8_t irqpin;
    
    struct pci_device_bar bar[PCI_MAX_BAR];
};

void init_pci();
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
void pci_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
void pci_scan_device(uint8_t bus, uint8_t device, uint8_t function);
struct pci_device *pci_get_device_ById(uint16_t vendorID, uint16_t deviceID);
struct pci_device *pci_get_device_ByClass(uint8_t classcode, uint8_t subclass);
void pci_enable_bus_mastering(struct pci_device *device);
uint32_t pci_get_device_connected(void);
struct pci_device *pci_alloc_device(void);
int pci_free_device(struct pci_device *dev);
void pci_device_init(struct pci_device *dev, uint8_t bus, uint8_t device, uint8_t func, uint16_t vendorID, uint16_t deviceID, uint32_t classcode, uint8_t revisionID, uint8_t multifunction);
void pci_bar_init(struct pci_device_bar *bar, uint32_t addr, uint32_t len);
uint32_t pci_device_read(struct pci_device *dev, uint32_t offset);
void pci_device_write(struct pci_device *dev, uint32_t offset, uint32_t value);
uint32_t pci_device_get_mem_addr(struct pci_device *dev);
uint32_t pci_device_get_io_addr(struct pci_device *dev);

#endif