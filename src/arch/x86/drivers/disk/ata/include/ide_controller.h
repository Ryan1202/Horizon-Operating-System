#ifndef _ATA_IDE_CONTROLL_H
#define _ATA_IDE_CONTROLL_H

#include "bits.h"
#include "driver/timer_dm.h"
#include "drivers/pci.h"
#include "kernel/driver_interface.h"
#include "stdint.h"

#define IDE_CONTROLLER_CLASSCODE 0x01
#define IDE_CONTROLLER_SUBCLASS	 0x01

#define IDE_REG_BM_COMMAND 0x00
#define IDE_REG_BM_STATUS  0x02
#define IDE_REG_BM_PRDT	   0x04

#define IDE_BMCMD_START_STOP_BM BIT(0)
#define IDE_BMCMD_READ_WRITE	BIT(3)

#define IDE_BMSTATUS_ACTIVE	  BIT(0)
#define IDE_BMSTATUS_ERROR	  BIT(1)
#define IDE_BMSTATUS_INT	  BIT(2)
#define IDE_BMSTATUS_DMA0_CAP BIT(5)
#define IDE_BMSTATUS_DMA1_CAP BIT(6)

extern struct BusDriverOps ide_bus_driver_ops;
extern struct BusDriver	   ide_bus_driver;

extern struct PciDriver ide_pci_driver;

typedef struct IdeChannelInfo {
	enum {
		IDE_COMPATITY_MODE,
		IDE_NATIVE_MODE,
	} mode;
	DeviceIrq *irq;
	uint16_t   io_base;
	uint16_t   control_base;
	uint16_t   bmide;
	uint8_t	   channel_num;
	uint8_t	   device_count;

	Timer timer;

	int									  selected_device;
	struct IdeDevice					 *ide_devices[2];
	struct PhysicalRegionDescriptorTable *prdt;
} IdeChannel;

typedef struct IdeControllerInfo {
	uint8_t dma_support;

	PciDevice *pci_device;
	uint32_t   bus_master_base;
} IdeControllerInfo;

extern struct DeviceDriver			 ide_controller_device_driver;
extern struct BusControllerDeviceOps ide_bus_controller_device_ops;

int	 ide_wait(IdeChannel *channel);
void ide_print_error(IdeChannel *channel);
void ide_reset_drive(IdeChannel *channel);
void ide_select_device(IdeChannel *channel, int device_num);

#endif