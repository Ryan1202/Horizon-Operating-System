#ifndef _ATA_IDE_H
#define _ATA_IDE_H

#include "ata.h"
#include "ata_driver.h"
#include "driver/storage/storage_io_queue.h"
#include "ide_controller.h"
#include "kernel/device.h"
#include "kernel/spinlock.h"
#include "stdint.h"

#define IDE_IRQ0 14
#define IDE_IRQ1 15

#define IDE_MAX_PRDT_COUNT 16

typedef struct IdeOps {
	void (*set_sector)(
		struct Device *device, uint32_t lba0, uint32_t lba1, uint32_t count);
} IdeOps;

typedef struct IdeDevice {
	Device			*device;
	IdeChannel		*channel;
	AtaIdentifyInfo *info;
	AtaDeviceType	 type;
	uint8_t			 device_num;
	enum {
		TRANSFER_MODE_PIO,
		TRANSFER_MODE_DMA,
	} mode;

	spinlock_t		request_lock;
	StorageRequest *current_request;

	AtaCmdIndex cmdset[ATA_CMDSET_MAX];
} IdeDevice;

extern struct DeviceDriver ide_device_driver;

void ide_channel0_handler(struct Device *device);
void ide_channel1_handler(struct Device *device);
void ide_device_probe(IdeChannel *channel);

#endif