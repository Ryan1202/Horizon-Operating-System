#include <bits.h>
#include <driver/interrupt/interrupt_dm.h>
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io_queue.h>
#include <driver/timer/timer_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/platform.h>
#include <kernel/spinlock.h>
#include <math.h>
#include <objects/transfer.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

#include "include/ata.h"
#include "include/ata_cmd.h"
#include "include/ata_driver.h"
#include "include/dma.h"
#include "include/ide.h"
#include "include/ide_controller.h"
#include "kernel/console.h"

DriverResult ide_device_init(void *device);

DriverResult ide_device_read_sectors(
	StorageDevice *storage_device, StorageRequest *request);
DriverResult ide_device_write_sectors(
	StorageDevice *storage_device, StorageRequest *request);
bool ide_device_is_busy(StorageDevice *storage_device);

StorageDeviceOps ide_storage_device_ops = {
	.submit_read_request  = ide_device_read_sectors,
	.submit_write_request = ide_device_write_sectors,
	.is_busy			  = ide_device_is_busy,
};
DeviceOps ide_device_ops = {
	.init	 = ide_device_init,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};

DeviceDriver ide_device_driver;

void ide_handle_interrupt(IdeChannel *channel) {
	IdeDevice *ide_device = channel->ide_devices[channel->selected_device];

	// DMA 模式检查 BM 状态
	if (channel->bmide && ide_device && ide_device->mode == TRANSFER_MODE_DMA) {
		int bm_status = io_in_byte(channel->bmide + IDE_REG_BM_STATUS);
		// 检查是否有 DMA 中断或错误
		if (!(bm_status & (IDE_BMSTATUS_INT | IDE_BMSTATUS_ERROR))) {
			// 可能是共享中断，读取 ATA 状态后返回
			io_in_byte(channel->io_base + ATA_REG_STATUS);
			return;
		}
		// 清除 BM 状态 (W1C: 只写入要清除的位)
		io_out_byte(
			channel->bmide + IDE_REG_BM_STATUS,
			IDE_BMSTATUS_INT | IDE_BMSTATUS_ERROR);
	}

	// 读取 ATA 状态清除中断
	int ata_status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	if (ata_status & ATA_STATUS_ERR) {
		print_error("IDE", "IDE device error!");
		ide_print_error(channel);
	}

	if (ide_device == NULL) { return; }

	int				flags		= spin_lock_irqsave(&ide_device->request_lock);
	StorageRequest *request		= ide_device->current_request;
	ide_device->current_request = NULL;
	spin_unlock_irqrestore(&ide_device->request_lock, flags);

	if (request == NULL) { return; }

	if (channel->bmide && ide_device->mode == TRANSFER_MODE_DMA) {
		uint8_t data = io_in_byte(channel->bmide + IDE_REG_BM_COMMAND);
		io_out_byte(
			channel->bmide + IDE_REG_BM_COMMAND,
			BIN_DIS(data, IDE_BMCMD_START_STOP_BM));

		ata_bmdma_unmap_buffer(
			channel->dma, request->buf, request->count * SECTOR_SIZE);
		if (request->rw == 0) { storage_solve_read_request(request); }
	}
	storage_finish_request(request);
}

void ide_irq_handler(void *channel) {
	IdeChannel *ide_channel = channel;

	ide_handle_interrupt(ide_channel);
}

void ide_sync(IdeChannel *channel) {
	// 保证先前的命令执行，而不是在缓存中
	io_in_byte(channel->control_base + ATA_REG_ALTSTATUS);
}

void ide_pause(IdeChannel *channel) {
	ide_sync(channel);
	delay_ms(&channel->timer, 1);
}

void ide_device_probe(IdeChannel *channel) {
	int			  i, status, err = 0;
	AtaDeviceType type = ATA_DEVICE_TYPE_ATA;

	timer_init(&channel->timer);
	channel->device_count = 0;

	StorageDevice *device[2] = {NULL, NULL};
	for (i = 0; i < 2; i++) {
		err	 = 0;
		type = ATA_DEVICE_TYPE_ATA;

		// 1.选择设备
		ide_select_device(channel, i);
		ide_pause(channel);

		// 2.发送IDENTIFY命令
		io_out_byte(
			channel->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_DEVICE);

		// 3.检查设备状态
		if (io_in_byte(channel->control_base + ATA_REG_ALTSTATUS) == 0) {
			continue; // 设备不存在
		}
		while (true) {
			status = io_in_byte(channel->control_base + ATA_REG_ALTSTATUS);
			if (BIN_IS_EN(status, ATA_STATUS_ERR)) {
				err = 1;
				break;
			}
			if (BIN_IS_DIS(status, ATA_STATUS_BUSY) &&
				BIN_IS_EN(status, ATA_STATUS_DRQ)) {
				break;
			}
		}

		// 4.检查设备类型
		if (err != 0) {
			uint8_t cl = io_in_byte(channel->io_base + ATA_REG_LBA1);
			uint8_t ch = io_in_byte(channel->io_base + ATA_REG_LBA2);

			if (cl == ~ch && (cl == 0x14 || cl == 0x69)) {
				type = ATA_DEVICE_TYPE_ATAPI;
				io_out_byte(
					channel->io_base + ATA_REG_COMMAND,
					ATA_CMD_IDENTIFY_PACKET_DEVICE);
				ide_wait(channel);
			} else {
				continue;
			}
		}

		// 5.读取设备信息
		AtaIdentifyInfo *identify = kmalloc(SECTOR_SIZE);
		io_stream_in_word(
			channel->io_base + ATA_REG_DATA, identify,
			sizeof(AtaIdentifyInfo) / 2);

		// IDENTIFY 数据阶段结束后读一次主状态，确保通道回到空闲状态。
		io_in_byte(channel->io_base + ATA_REG_STATUS);

		// 6.注册设备
		create_storage_device(
			&device[i], &ide_storage_device_ops, &ide_device_ops,
			channel->physical_device, &ide_device_driver);

		IdeDevice *ide_device			 = kmalloc(sizeof(IdeDevice));
		device[i]->device->private_data	 = ide_device;
		device[i]->block_size			 = SECTOR_SIZE;
		device[i]->max_block_per_request = 256;
		device[i]->max_segment			 = IDE_MAX_PRDT_COUNT;
		device[i]->type					 = STORAGE_DEVICE_TYPE_HARDDISK;

		ide_device->device			= device[i]->device;
		ide_device->channel			= channel;
		ide_device->type			= type;
		ide_device->info			= identify;
		ide_device->device_num		= i;
		ide_device->current_request = NULL;
		spinlock_init(&ide_device->request_lock);

		channel->ide_devices[i] = ide_device;

		channel->device_count++;
	}
	if (channel->device_count) {
		channel->control = BIN_DIS(channel->control, ATA_CONTROL_NIEN);
		io_out_byte(channel->control_base + ATA_REG_CONTROL, channel->control);

		channel->dma = kmalloc(sizeof(AtaDma));
		channel->dma->prds =
			kmalloc(sizeof(PhysicalRegionDescriptor) * IDE_MAX_PRDT_COUNT);
		memset(
			channel->dma->prds, 0,
			sizeof(PhysicalRegionDescriptor) * IDE_MAX_PRDT_COUNT);
		channel->dma->prdt_phy_addr	   = vir2phy((size_t)channel->dma->prds);
		channel->dma->prdt_status	   = 0;
		channel->dma->max_segment_size = 65536;
		list_init(&channel->dma->segment_lh);

		enable_device_irq(channel->irq);

		for (i = 0; i < 2; i++) {
			if (device[i] != NULL) {
				init_and_start_logical_device(device[i]->device);
			}
		}
	}
}

DriverResult ide_device_init(void *_device) {
	LogicalDevice	*device		= _device;
	IdeDevice		*ide_device = device->private_data;
	AtaIdentifyInfo *identify	= ide_device->info;

	if (identify->capabilities.dma_supported) {
		ide_device->mode						 = TRANSFER_MODE_DMA;
		ide_device->cmdset[ATA_CMDSET_READ_EXT]	 = ATA_CMD_READ_DMA_EXT;
		ide_device->cmdset[ATA_CMDSET_WRITE_EXT] = ATA_CMD_WRITE_DMA_EXT;
		ide_device->cmdset[ATA_CMDSET_READ]		 = ATA_CMD_READ_DMA;
		ide_device->cmdset[ATA_CMDSET_WRITE]	 = ATA_CMD_WRITE_DMA;
	} else if (identify->pio_modes_supported) {
		ide_device->mode						 = TRANSFER_MODE_PIO;
		ide_device->cmdset[ATA_CMDSET_READ_EXT]	 = ATA_CMD_READ_PIO_EXT;
		ide_device->cmdset[ATA_CMDSET_WRITE_EXT] = ATA_CMD_WRITE_PIO_EXT;
		ide_device->cmdset[ATA_CMDSET_READ]		 = ATA_CMD_READ_PIO;
		ide_device->cmdset[ATA_CMDSET_WRITE]	 = ATA_CMD_WRITE_PIO;
	} else {
		return DRIVER_ERROR_UNSUPPORT_FEATURE;
	}

	return DRIVER_OK;
}

void ide_set_sector_lba28(IdeChannel *channel, uint32_t lba0, uint8_t count) {
	io_out_byte(channel->io_base + ATA_REG_SECCOUNT0, count);
	io_out_byte(channel->io_base + ATA_REG_LBA0, lba0 & 0xff);
	io_out_byte(channel->io_base + ATA_REG_LBA1, (lba0 >> 8) & 0xff);
	io_out_byte(channel->io_base + ATA_REG_LBA2, (lba0 >> 16) & 0xff);
}

void ide_set_sector_lba48(
	IdeChannel *channel, uint32_t lba0, uint32_t lba1, uint16_t count) {
	io_out_byte(channel->io_base + ATA_REG_SECCOUNT1, count >> 8);
	io_out_byte(channel->io_base + ATA_REG_LBA3, lba1 & 0xff);
	io_out_byte(channel->io_base + ATA_REG_LBA4, (lba1 >> 8) & 0xff);
	io_out_byte(channel->io_base + ATA_REG_LBA5, (lba1 >> 16) & 0xff);

	io_out_byte(channel->io_base + ATA_REG_SECCOUNT0, count);
	io_out_byte(channel->io_base + ATA_REG_LBA0, lba0 & 0xff);
	io_out_byte(channel->io_base + ATA_REG_LBA1, (lba0 >> 8) & 0xff);
	io_out_byte(channel->io_base + ATA_REG_LBA2, (lba0 >> 16) & 0xff);
}

/**
 * 通过PIO方式接收数据
 */
void ide_device_recv_pio(IdeDevice *device, size_t *buf, uint32_t count) {
	IdeChannel *channel = device->channel;

	ide_wait(channel);
	io_stream_in_word(
		channel->io_base + ATA_REG_DATA, buf, count << 8 /* count * 512 / 2 */);
}

/**
 * 通过PIO方式发送数据
 */
void ide_device_send_pio(IdeDevice *device, size_t *buf, uint32_t count) {
	IdeChannel *channel = device->channel;

	ide_wait(channel);
	io_stream_out_word(
		channel->io_base + ATA_REG_DATA, buf, count << 8 /* count * 512 / 2 */);
}

/**
 * 从设备读取扇区，调用方保证buf与count的合法性
 *
 * lba0 & lba1: lba地址,最大支持48位
 *
 * count: 扇区数，最大为65535
 */
DriverResult ide_device_read_sectors(
	StorageDevice *storage_device, StorageRequest *request) {
	IdeDevice  *ide_device = storage_device->device->private_data;
	IdeChannel *channel	   = ide_device->channel;

	bool flag = false;

	request->count = MIN(request->count, IDE_MAX_PRDT_COUNT * 128 - 1);
	// 因为没有实现对28位地址的处理，所以超过24位都使用48位地址
	if ((request->position < 0x1000000) || request->count <= 0x100) {
		flag = true;
	}

	if (ide_device->mode == TRANSFER_MODE_DMA) {
		ata_bmdma_map_buffer(
			channel->dma, request->buf, request->count * SECTOR_SIZE);
		ata_bmdma_set_prdt(ide_device, channel->dma, request->rw);
	}

	ide_select_device(channel, ide_device->device_num);
	ide_sync(channel);
	ide_wait_cmd_ready(channel);

	int status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	if (status & ATA_STATUS_DRQ) {
		print_error(
			"IDE", "DRQ still set before READ_DMA, status=%#x\n", status);
		return DRIVER_ERROR_OTHER;
	}

	uint8_t cmd;
	if (flag) {
		ide_set_sector_lba28(channel, request->position, request->count);
		cmd = ide_device->cmdset[ATA_CMDSET_READ];
	} else {
		ide_set_sector_lba48(
			channel, request->position & 0xffffff, request->position >> 24,
			request->count);
		cmd = ide_device->cmdset[ATA_CMDSET_READ_EXT];
	}

	ide_wait_cmd_ready(channel);

	int flags					= spin_lock_irqsave(&ide_device->request_lock);
	ide_device->current_request = request;
	if (ide_device->mode == TRANSFER_MODE_PIO) {
		io_out_byte(channel->io_base + ATA_REG_COMMAND, cmd);
		ide_device_recv_pio(ide_device, (size_t *)request->buf, request->count);
	} else {
		uint8_t data = io_in_byte(channel->bmide + IDE_REG_BM_COMMAND);
		io_out_byte(
			channel->bmide + IDE_REG_BM_COMMAND,
			BIN_EN(data, IDE_BMCMD_START_STOP_BM));
		io_out_byte(channel->io_base + ATA_REG_COMMAND, cmd);
	}
	spin_unlock_irqrestore(&ide_device->request_lock, flags);
	return DRIVER_OK;
}

/**
 * 向设备写入扇区，调用方保证buf与count的合法性
 *
 * lba0 & lba1: lba地址,最大支持48位
 *
 * count: 扇区数，最大为65535
 */
DriverResult ide_device_write_sectors(
	StorageDevice *storage_device, StorageRequest *request) {
	IdeDevice  *ide_device = storage_device->device->private_data;
	IdeChannel *channel	   = ide_device->channel;

	bool flag = false;

	// 因为没有实现对28位地址的处理，所以超过24位都使用48位地址
	if ((request->position < 0x1000000) || request->count < 0x100) {
		flag = true;
	}

	if (ide_device->mode == TRANSFER_MODE_DMA) {
		storage_solve_write_request(request);
		ata_bmdma_map_buffer(
			channel->dma, request->buf, request->count * SECTOR_SIZE);
		ata_bmdma_set_prdt(ide_device, channel->dma, request->rw);
	}

	ide_select_device(channel, ide_device->device_num);
	ide_sync(channel);
	ide_wait_cmd_ready(channel);

	int status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	if (status & ATA_STATUS_DRQ) {
		print_error(
			"IDE", "DRQ still set before WRITE_DMA, status=%#x\n", status);
		return DRIVER_ERROR_OTHER;
	}

	uint8_t cmd;
	if (flag) {
		ide_set_sector_lba28(channel, request->position, request->count);
		cmd = ide_device->cmdset[ATA_CMDSET_WRITE];
	} else {
		ide_set_sector_lba48(
			channel, request->position & 0xffffff, request->position >> 24,
			request->count);
		cmd = ide_device->cmdset[ATA_CMDSET_WRITE_EXT];
	}

	ide_wait_cmd_ready(channel);
	io_out_byte(channel->io_base + ATA_REG_COMMAND, cmd);

	int flags					= spin_lock_irqsave(&ide_device->request_lock);
	ide_device->current_request = request;
	if (ide_device->mode == TRANSFER_MODE_PIO) {
		ide_device_send_pio(ide_device, (size_t *)request->buf, request->count);
		ide_device->current_request = NULL;
	} else {
		uint8_t data = io_in_byte(channel->bmide + IDE_REG_BM_COMMAND);
		io_out_byte(
			channel->bmide + IDE_REG_BM_COMMAND,
			BIN_EN(data, IDE_BMCMD_START_STOP_BM));
	}
	spin_unlock_irqrestore(&ide_device->request_lock, flags);
	return DRIVER_OK;
}

bool ide_device_is_busy(StorageDevice *storage_device) {
	LogicalDevice *device	  = storage_device->device;
	IdeDevice	  *ide_device = device->private_data;

	int	 flags	= spin_lock_irqsave(&ide_device->request_lock);
	bool result = ide_device->current_request != NULL;
	spin_unlock_irqrestore(&ide_device->request_lock, flags);

	return result;
}
