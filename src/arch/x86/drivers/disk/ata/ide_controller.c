#include "include/ide_controller.h"
#include "include/ata_driver.h"
#include "include/ide.h"
#include <bits.h>
#include <driver/bus_dm.h>
#include <driver/interrupt/interrupt_dm.h>
#include <drivers/bus/pci/pci.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

DriverResult ide_controller_probe(
	PciDevice *pci_device, PhysicalDevice *physical_device);
DriverResult ide_controller_init(void *device);

DeviceDriver ide_controller_device_driver;

DeviceOps ide_controller_device_ops = {
	.init	 = ide_controller_init,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};

PciDriverOps ide_pci_driver_ops = {
	.probe = ide_controller_probe,
};
PciDriver ide_pci_driver = {
	.find_type		= FIND_BY_CLASSCODE_SUBCLASS,
	.class_subclass = {IDE_CONTROLLER_CLASSCODE, IDE_CONTROLLER_SUBCLASS},
	.device_driver	= &ide_controller_device_driver,
	.driver			= &ata_driver,
	.ops			= &ide_pci_driver_ops,
};

void ide_detect_channel_mode(
	IdeChannel *channel, PciDevice *pci_device, int i) {
	int bit = 1 << (i * 2);
	if (pci_device->prog_if & bit) {
		if (pci_device->prog_if & (bit << 1)) {
			channel->channel_num = i;
			channel->mode		 = IDE_NATIVE_MODE;
			pci_device->prog_if |= bit;
			pci_device_write8(pci_device, PCI_REG_PROGIF, pci_device->prog_if);
		} else {
			channel->channel_num = i;
			channel->mode		 = IDE_COMPATITY_MODE;
		}
	}
}

DriverResult ide_controller_probe(
	PciDevice *pci_device, PhysicalDevice *physical_device) {
	IdeControllerInfo *info = kzalloc(sizeof(IdeControllerInfo));

	register_physical_device(physical_device, &ide_controller_device_ops);

	physical_device->private_data = info;
	info->pci_device			  = pci_device;
	info->dma_support			  = pci_device->prog_if >> 7;
	info->bus_master_base		  = pci_device->common.bar[4].base_addr;

	return DRIVER_OK;
}

void ide_controller_setup_legacy_mode(
	PhysicalDevice *physical_device, IdeChannel *channel) {
	channel->io_base =
		(channel->channel_num ? ATA_SECONDARY_PORT : ATA_PRIMARY_PORT);
	channel->control_base =
		(channel->channel_num ? ATA_SECONDARY_CONTROL_PORT
							  : ATA_PRIMARY_CONTROL_PORT);
	register_device_irq(
		&channel->irq, physical_device, channel,
		(channel->channel_num ? IDE_IRQ1 : IDE_IRQ0), ide_irq_handler,
		IRQ_MODE_SHARED);
}

void ide_controller_setup_pci_mode(PciDevice *pci_device, IdeChannel *channel) {
	channel->io_base =
		pci_device->common.bar[channel->channel_num * 2].base_addr;
	channel->control_base =
		pci_device->common.bar[channel->channel_num * 2 + 1].base_addr;
	register_device_irq(
		&channel->irq, pci_device->device, channel, pci_device->irqline,
		ide_irq_handler, IRQ_MODE_SHARED);
}

DriverResult ide_controller_init(void *_device) {
	PhysicalDevice	  *device = _device;
	IdeControllerInfo *info	  = device->private_data;

	for (int i = 0; i < 2; i++) {
		IdeChannel *channel = &info->channels[i];

		channel->physical_device = device;
		channel->channel_num	 = i;
		channel->selected_device = 0;
		// 检查是否支持PCI Native模式
		ide_detect_channel_mode(channel, info->pci_device, i);

		if (channel->mode == IDE_COMPATITY_MODE) {
			ide_controller_setup_legacy_mode(device, channel);
		} else {
			ide_controller_setup_pci_mode(info->pci_device, channel);
		}

		if (BIN_IS_EN(info->pci_device->prog_if, BIT(7))) {
			channel->bmide = info->pci_device->common.bar[4].base_addr;
			channel->bmide += (channel->channel_num ? 8 : 0);
			pci_enable_bus_mastering(info->pci_device);
		}

		// 禁用并注册IRQ
		io_out_byte(channel->control_base + ATA_REG_CONTROL, ATA_CONTROL_NIEN);

		ide_device_probe(channel);
	}

	return DRIVER_OK;
}

void ide_print_error(IdeChannel *channel) {
	int status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	if (status & ATA_STATUS_DF) {
		print_error("IDE", "Device Fault!\n");
	} else if (status & ATA_STATUS_ERR) {
		int err = io_in_byte(channel->io_base + ATA_REG_ERROR);
		if (err & ATA_ERROR_AMNF) {
			print_error("IDE", "No Address Mark Found\n");
		}
		if (err & ATA_ERROR_TK0NF) {
			print_error("IDE", "No Media or Media Mark Found\n");
		}
		if (err & ATA_ERROR_ABRT) { print_error("IDE", "Command Aborted\n"); }
		if (err & ATA_ERROR_MCR) {
			print_error("IDE", "No Media or Media Error\n");
		}
		if (err & ATA_ERROR_IDNF) { print_error("IDE", "ID mark not Found\n"); }
		if (err & ATA_ERROR_MC) {
			print_error("IDE", "No Media or Media Error\n");
		}
		if (err & ATA_ERROR_UNC) {
			print_error("IDE", "Uncorrectable Data Error\n");
		}
		if (err & ATA_ERROR_BBK) { print_error("IDE", "Bad Sectors\n"); }
	} else if (status & ATA_STATUS_DRQ) {
		print_error("IDE", "Reads Nothing\n");
	}
}

void ide_reset_drive(IdeChannel *channel) {
	uint8_t data = io_in_byte(channel->control_base + ATA_REG_CONTROL);
	io_out_byte(channel->control_base + ATA_REG_CONTROL, BIN_EN(data, BIT(2)));

	// 等待重置
	int i;
	for (i = 0; i < 50; i++) {
		io_in_byte(channel->io_base + ATA_REG_STATUS);
	}
	io_out_byte(channel->control_base + ATA_REG_CONTROL, data);
	channel->selected_device = 0;
}

void ide_select_device(IdeChannel *channel, int device_num) {
	io_out_byte(
		channel->io_base + ATA_REG_HDDEV_SEL,
		device_num << 4 | BIT(6) | BIT(5) | BIT(7));
	channel->selected_device = device_num;
}

int ide_wait(IdeChannel *channel) {
	int status;
	do {
		status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	} while (BIN_IS_EN(status, ATA_STATUS_BUSY));
	return 0;
}

void ide_polling(IdeChannel *channel) {

	// 等待400ns
	for (int i = 0; i < 4; i++) {
		io_in_byte(
			channel->control_base + ATA_REG_ALTSTATUS); // 读一次需要100ns
	}

	int status;
	do {
		status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	} while (BIN_IS_EN(status, ATA_STATUS_BUSY));

	if (BIN_IS_EN(status, ATA_STATUS_ERR)) {
		print_error_with_position("in ide_select_device(): IDE Error!\n");
	}
}
