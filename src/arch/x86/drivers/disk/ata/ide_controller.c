#include <bits.h>
#include <driver/bus_dm.h>
#include <driver/interrupt_dm.h>
#include <drivers/bus/pci/pci.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/memory.h>
#include <stdint.h>
#include <string.h>

#include "include/ata_driver.h"
#include "include/ide.h"
#include "include/ide_controller.h"
#include "objects/object.h"

DriverResult ide_controller_probe(PciDevice *pci_device);
DriverResult ide_controller_init(Device *device);

DeviceDriverOps ide_controller_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
DeviceDriver ide_controller_device_driver = {
	.name	  = STRING_INIT("IDE Controller Device Driver"),
	.type	  = DEVICE_TYPE_BUS_CONTROLLER,
	.priority = DRIVER_PRIORITY_BASIC,
	.state	  = DRIVER_STATE_UNREGISTERED,
	.ops	  = &ide_controller_device_driver_ops,
};

DeviceOps ide_controller_device_ops = {
	.init	 = ide_controller_init,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};
Device ide_controller_device_templete = {
	.name			   = STRING_INIT("IDE Controller"),
	.state			   = DEVICE_STATE_UNREGISTERED,
	.device_driver	   = &ide_controller_device_driver,
	.ops			   = &ide_controller_device_ops,
	.max_child_device  = 2,
	.private_data_size = sizeof(IdeControllerInfo),
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

DriverResult ide_controller_probe(PciDevice *pci_device) {
	Device	  *device = kmalloc_from_template(ide_controller_device_templete);
	string_t   name	  = STRING_INIT("");
	ObjectAttr attr	  = device_object_attr;
	register_device(
		&ide_controller_device_driver, name, pci_device->bus, device, &attr);

	IdeControllerInfo *info = device->private_data;
	info->pci_device		= pci_device;
	info->dma_support		= pci_device->prog_if >> 7;
	info->bus_master_base	= pci_device->common.bar[4].base_addr;

	return DRIVER_RESULT_OK;
}

void ide_controller_setup_legacy_mode(IdeChannel *channel) {
	channel->io_base =
		(channel->channel_num ? ATA_SECONDARY_PORT : ATA_PRIMARY_PORT);
	channel->control_base =
		(channel->channel_num ? ATA_SECONDARY_CONTROL_PORT
							  : ATA_PRIMARY_CONTROL_PORT);
	channel->irq->irq = (channel->channel_num ? IDE_IRQ1 : IDE_IRQ0);
}

void ide_controller_setup_pci_mode(PciDevice *pci_device, IdeChannel *channel) {
	channel->io_base =
		pci_device->common.bar[channel->channel_num * 2].base_addr;
	channel->control_base =
		pci_device->common.bar[channel->channel_num * 2 + 1].base_addr;
	channel->irq->irq = pci_device->irqline;
}

DriverResult ide_controller_init(Device *device) {
	IdeControllerInfo *info = device->private_data;

	for (int i = 0; i < 2; i++) {
		register_child_device(device, sizeof(IdeChannel));

		IdeChannel *channel = device->child_private_data[i];

		channel->channel_num = i;
		// 检查是否支持PCI Native模式
		ide_detect_channel_mode(channel, info->pci_device, i);

		DeviceIrq *irq = kmalloc(sizeof(DeviceIrq));
		irq->device	   = device;
		irq->handler =
			(channel->channel_num == 0 ? ide_channel0_handler
									   : ide_channel1_handler);
		channel->irq = irq;

		if (channel->mode == IDE_COMPATITY_MODE) {
			ide_controller_setup_legacy_mode(channel);
		} else {
			ide_controller_setup_pci_mode(info->pci_device, channel);
		}

		if (BIN_IS_EN(info->pci_device->prog_if, BIT(7))) {
			channel->bmide = info->pci_device->common.bar[4].base_addr;
			channel->bmide += (channel->channel_num ? 8 : 0);
			pci_enable_bus_mastering(info->pci_device);
		}

		// 禁用并注册IRQ
		io_out_byte(channel->io_base + ATA_REG_CONTROL, ATA_CONTROL_NIEN);
		register_device_irq(irq);

		ide_device_probe(channel);
	}

	return DRIVER_RESULT_OK;
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
	uint8_t data = io_in_byte(channel->io_base + ATA_REG_CONTROL);
	io_out_byte(channel->io_base + ATA_REG_CONTROL, BIN_EN(data, BIT(2)));

	// 等待重置
	int i;
	for (i = 0; i < 50; i++) {
		io_in_byte(channel->io_base + ATA_REG_STATUS);
	}
	io_out_byte(channel->io_base + ATA_REG_CONTROL, data);
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
		io_in_byte(channel->io_base + ATA_REG_ALTSTATUS); // 读一次需要100ns
	}

	int status;
	do {
		status = io_in_byte(channel->io_base + ATA_REG_STATUS);
	} while (BIN_IS_EN(status, ATA_STATUS_BUSY));

	if (BIN_IS_EN(status, ATA_STATUS_ERR)) {
		print_error_with_position("in ide_select_device(): IDE Error!\n");
	}
}
