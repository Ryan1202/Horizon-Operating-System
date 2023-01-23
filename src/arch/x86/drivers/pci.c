/**
 * @file pci.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2020-07
 */
#include <drivers/pci.h>
#include <kernel/console.h>
#include <kernel/func.h>
#include <types.h>

struct pci_device pci_devices[PCI_MAX_DEVICE];

void init_pci()
{
    int i, j, k;

    printk("device id\tvendor id\theader type\tclasscode\tsubclass\tprogif\trevision id\n");
    for (i = 0; i < PCI_MAX_DEVICE; i++) {
        pci_devices[i].status = PCI_DEVICE_STATUS_INVALID;
    }
    for (i = 0; i < PCI_MAX_BUS; i++) {
        for (j = 0; j < PCI_MAX_DEV; j++) {
            for (k = 0; k < PCI_MAX_FUNC; k++) {
                pci_scan_device(i, j, k);
            }
        }
    }
}

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

void pci_scan_device(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t value    = pci_read32(bus, device, function, 0);
    uint16_t vendorID = value & 0xffff;
    uint16_t deviceID = value >> 16;
    if (vendorID == 0xffff) {
        return;
    }
    value               = pci_read32(bus, device, function, 0x0c);
    uint8_t header_type = value >> 16;
    value               = pci_read32(bus, device, function, 8);
    uint32_t classcode  = value >> 8;
    uint8_t  revisionID = value & 0xff;

    struct pci_device *dev = pci_alloc_device();
    if (dev == NULL) {
        return;
    }
    pci_device_init(dev, bus, device, function, vendorID, deviceID, classcode, revisionID, header_type);

    if (header_type == 0x00) {
        int bar;
        for (bar = 0; bar < PCI_MAX_BAR; bar++) {
            value = pci_read32(bus, device, function, PCI_BAR(bar));
            pci_write32(bus, device, function, PCI_BAR(bar), 0xffffffff);
            uint32_t len = pci_read32(bus, device, function, PCI_BAR(bar));
            pci_write32(bus, device, function, PCI_BAR(bar), value);

            if (len != 0 && len != 0xffffffff) {
                pci_bar_init(&dev->bar[bar], value, len);
            }
        }
    }

    value = pci_read32(bus, device, function, 0x3c) & 0xffff;
    if ((value & 0xff) > 0 && (value & 0xff) < 32) {
        dev->irqline = value & 0xff;
        dev->irqpin  = value >> 8;
    }

    printk("%#4x\t\t%#x\t\t%#x\t\t\t%#x\t\t\t%#x\t\t\t%#x\t\t%#x\n", deviceID, vendorID, header_type & (uint8_t)(~0x80), dev->classcode, dev->subclass, dev->prog_if, revisionID);
}

struct pci_device *pci_get_device_ById(uint16_t vendorID, uint16_t deviceID)
{
    int                i;
    struct pci_device *device;

    for (i = 0; i < PCI_MAX_DEVICE; i++) {
        device = &pci_devices[i];
        if (device->vendorID == vendorID && device->deviceID == deviceID) {
            return device;
        }
    }
    return NULL;
}

struct pci_device *pci_get_device_ByClassFull(uint8_t classcode, uint8_t subclass, uint8_t progif)
{
    int                i;
    struct pci_device *device;

    for (i = 0; i < PCI_MAX_DEVICE; i++) {
        device = &pci_devices[i];
        if (device->classcode == classcode && device->subclass == subclass && device->prog_if == progif) {
            return device;
        }
    }
    return NULL;
}

struct pci_device *pci_get_device_ByClass(uint8_t classcode, uint8_t subclass)
{
    int                i;
    struct pci_device *device;

    for (i = 0; i < PCI_MAX_DEVICE; i++) {
        device = &pci_devices[i];
        if (device->classcode == classcode && device->subclass == subclass) {
            return device;
        }
    }
    return NULL;
}

void pci_enable_bus_mastering(struct pci_device *device)
{
    uint32_t value = pci_read32(device->bus, device->dev, device->function, 0x04);
    value |= 4;
    pci_write32(device->bus, device->dev, device->function, 0x04, value);
}

void pci_enable_io_space(struct pci_device *device)
{
    uint32_t value = pci_read32(device->bus, device->dev, device->function, 0x04);
    value |= 1;
    pci_write32(device->bus, device->dev, device->function, 0x04, value);
}

void pci_enable_mem_space(struct pci_device *device)
{
    uint32_t value = pci_read32(device->bus, device->dev, device->function, 0x04);
    value |= 2;
    pci_write32(device->bus, device->dev, device->function, 0x04, value);
}

uint32_t pci_get_device_connected(void)
{
    int                i;
    struct pci_device *device;
    for (i = 0; i < PCI_MAX_BAR; i++) {
        device = &pci_devices[i];
        if (device->status != PCI_DEVICE_STATUS_USING) {
            break;
        }
    }
    return i;
}

struct pci_device *pci_alloc_device(void)
{
    int i;

    for (i = 0; i < PCI_MAX_DEVICE; i++) {
        if (pci_devices[i].status == PCI_DEVICE_STATUS_INVALID) {
            pci_devices[i].status = PCI_DEVICE_STATUS_USING;
            return &pci_devices[i];
        }
    }
    return NULL;
}

int pci_free_device(struct pci_device *dev)
{
    int i;

    for (i = 0; i < PCI_MAX_DEVICE; i++) {
        if (&pci_devices[i] == dev) {
            dev->status = PCI_DEVICE_STATUS_INVALID;
            return 0;
        }
    }
    return -1;
}

void pci_device_init(struct pci_device *dev,
                     uint8_t bus, uint8_t device, uint8_t func, uint16_t vendorID, uint16_t deviceID,
                     uint32_t classcode, uint8_t revisionID, uint8_t multifunction)
{
    int i;

    dev->bus           = bus;
    dev->dev           = device;
    dev->function      = func;
    dev->vendorID      = vendorID;
    dev->deviceID      = deviceID;
    dev->classcode     = classcode >> 16;
    dev->subclass      = (classcode & 0xff00) >> 8;
    dev->prog_if       = classcode & 0xff;
    dev->revisionID    = revisionID;
    dev->multifunction = multifunction;

    for (i = 0; i < PCI_MAX_BAR; i++) {
        dev->bar[i].type = PCI_BAR_TYPE_INVALID;
    }
    dev->irqline = -1;
}

void pci_bar_init(struct pci_device_bar *bar, uint32_t addr, uint32_t len)
{
    if (addr == 0xffffffff) {
        addr = 0;
    }
    if (addr & 1) // I/O内存
    {
        bar->type      = PCI_BAR_TYPE_IO;
        bar->base_addr = addr & PCI_BAR_IO_MASK;
        bar->length    = ~(len & PCI_BAR_IO_MASK) + 1;
    } else {
        bar->type      = PCI_BAR_TYPE_MEM;
        bar->base_addr = addr & PCI_BAR_MEM_MASK;
        bar->length    = ~(len & PCI_BAR_MEM_MASK) + 1;
    }
}

uint32_t pci_device_get_mem_addr(struct pci_device *dev)
{
    int i;

    for (i = 0; i < PCI_MAX_BAR; i++) {
        if (dev->bar[i].type == PCI_BAR_TYPE_MEM) {
            return dev->bar[i].base_addr;
        }
    }
    return -1;
}

uint32_t pci_device_get_io_addr(struct pci_device *dev)
{
    int i;

    for (i = 0; i < PCI_MAX_BAR; i++) {
        if (dev->bar[i].type == PCI_BAR_TYPE_IO) {
            return dev->bar[i].base_addr;
        }
    }
    return -1;
}
