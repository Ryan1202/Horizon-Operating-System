/**
 * @file uhci.c
 * @author Ryan Wang (Ryan1202@foxmail.com)
 * @brief UHCI驱动程序
 * @version 0.1
 * @date 2022-09-10
 *
 * 目前仅仅只是初始化了UHCI而已，其他涉及USB协议的东西都还没做
 *
 */
#include "../hcd.h"
#include <bits.h>
#include <driver/interrupt/interrupt_dm.h>
#include <driver/timer/timer_dm.h>
#include <drivers/bus/pci/pci.h>
#include <drivers/bus/usb/hcd.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/pit.h>
#include <drivers/usb/core/descriptors.h>
#include <drivers/usb/core/hub.h>
#include <drivers/usb/core/usb.h>
#include <drivers/usb/uhci.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/platform.h>
#include <kernel/softirq.h>
#include <kernel/thread.h>
#include <objects/attr.h>
#include <stdint.h>
#include <string.h>

extern bool usb_legacy_support_disabled;

#define UHCI_CLASSID	0x0c
#define UHCI_SUBCLASSID 0x03
#define UHCI_PROGIF		0x00

#define DRV_NAME	  "Universal Serial Bus(USB) Driver"
#define DEV_NAME	  "UHCI"
#define DEV_FULL_NAME "Universal Host Controller Interface(UHCI)"

extern UsbHcdOps uhci_hcd_ops;

uint32_t	   uhci_get_hub_status(UsbHub *hub);
uint32_t	   uhci_get_port_status(UsbHub *hub, uint8_t port);
UsbSetupStatus uhci_clear_port_feature(
	UsbHub *hub, uint8_t port, uint16_t feature);
UsbSetupStatus uhci_set_port_feature(
	UsbHub *hub, uint8_t port, uint16_t feature);
void		 uhci_register(Driver *driver);
DriverResult uhci_init(void *_device);
DriverResult uhci_start(void *_device);
DriverResult uhci_pci_probe(
	PciDevice *pci_device, PhysicalDevice *physical_device);
void uhci_port_reset(Uhci *uhci, int port);
void uhci_port_init(UsbHub *hub, UsbHcd *hcd, int port);

PciDriverOps uhci_pci_driver_ops = {
	.probe = uhci_pci_probe,
};
DeviceOps uhci_logical_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.destroy = NULL,
	.stop	 = NULL,
};
DeviceOps uhci_device_ops = {
	.init	 = uhci_init,
	.start	 = uhci_start,
	.destroy = NULL,
	.stop	 = NULL,
};
UsbHubOps uhci_root_hub_ops = {
	.init				= NULL,
	.clear_port_feature = uhci_clear_port_feature,
	.set_port_feature	= uhci_set_port_feature,
	.get_hub_status		= uhci_get_hub_status,
	.get_port_status	= uhci_get_port_status,
};

DeviceDriver uhci_device_driver;
PciDriver	 uhci_pci_driver = {
	   .driver				  = &usb_driver,
	   .device_driver		  = &uhci_device_driver,
	   .find_type			  = FIND_BY_CLASSCODE_SUBCLASS_PROGIF,
	   .class_subclass_progif = {UHCI_CLASSID, UHCI_SUBCLASSID, UHCI_PROGIF},
	   .ops					  = &uhci_pci_driver_ops,
};

void uhci_handler(void *arg) {
	Uhci   *uhci = arg;
	UhciQh *qhs	 = uhci->skel->qh, *qh;

	// 清除UHCI状态寄存器
	uint16_t status = io_in_word(uhci->io_base + UHCI_REG_STS);
	if (status == 0) return;
	if (status & UHCI_STAT_INTERRUPT) {
		io_out_word(uhci->io_base + UHCI_REG_STS, UHCI_STAT_INTERRUPT);
	}
	if (status & UHCI_STAT_ERROR_INT) {
		print_error("UHCI", "Error Interrupt");
		io_out_word(uhci->io_base + UHCI_REG_STS, UHCI_STAT_ERROR_INT);
	}
	if (status & UHCI_STAT_RESUME_DETECT) {
		print_error("UHCI", "Resume Detect");
		io_out_word(uhci->io_base + UHCI_REG_STS, UHCI_STAT_RESUME_DETECT);
	}
	if (status & UHCI_STAT_HOST_SYSTEM_ERROR) {
		print_error("UHCI", "Host System Error");
		io_out_word(uhci->io_base + UHCI_REG_STS, UHCI_STAT_HOST_SYSTEM_ERROR);
	}
	if (status & UHCI_STAT_HC_PROCESS_ERROR) {
		print_error("UHCI", "HC Process Error");
		io_out_word(uhci->io_base + UHCI_REG_STS, UHCI_STAT_HC_PROCESS_ERROR);
	}
	if (status & UHCI_STAT_HC_HALTED) {
		print_error("UHCI", "HC Halted");
		io_out_word(uhci->io_base + UHCI_REG_STS, UHCI_STAT_HC_HALTED);
	}

	for (int i = 0; i < UHCI_SKEL_QH_COUNT; i++) {
		if (qhs[i].qe_link == UHCI_TERMINATE) continue;
		if (!(qhs[i].qe_link & UHCI_QH_TD_SELECT)) continue;
		// 每一个Endpoint都有一个对应的QH，这个QH下只有TD
		qh = (UhciQh *)qhs[i].first_qh;
		if (qh->endpoint == NULL) continue;
		while (qh != NULL && qh->first_td != NULL) {
			UhciTd *td = (UhciTd *)qh->first_td;
			qh		   = qh->next;
			while (td != NULL) {
				if (td->active || !td->interrupt_on_complete) {
					td = td->next;
					continue;
				}
				// 传输完成
				td->interrupt_on_complete = 0;
				if (td->urb != NULL) {
					td->urb->actual_len += td->actlen;
					td->urb->status =
						td->stalled ? USB_STATUS_STALL
						: (td->crc_timeout_Error | td->bitstuff_Error |
						   td->databuffer_Error)
							? USB_STATUS_ERR
						: td->NAK_received ? USB_STATUS_NAK
										   : USB_STATUS_ACK;
					list_add_tail(&td->urb->list, &urb_lh);
					pending_softirq();
				}
				td = td->next;
			}
		}
	}
}

UsbSetupStatus uhci_clear_port_feature(
	UsbHub *hub, uint8_t port, uint16_t feature) {
	UsbHcd	*hcd	 = hub->hcd;
	Uhci	*uhci	 = (Uhci *)hcd->device->private_data;
	uint32_t io_port = uhci->io_base + UHCI_PORTSC1 + port * 2;
	uint16_t value	 = io_in_word(io_port);
	switch (feature) {
	case HUB_FEAT_PORT_ENABLE:
		io_out_word(io_port, BIN_DIS(value, UHCI_PORT_SC_ENABLE));
		break;
	case HUB_FEAT_C_PORT_CONNECTION:
		io_out_word(io_port, BIN_EN(value, UHCI_PORT_SC_CONN_CHG));
		break;
	case HUB_FEAT_C_PORT_ENABLE:
		io_out_word(io_port, BIN_EN(value, UHCI_PORT_SC_EN_CHG));
		break;
	case HUB_FEAT_C_PORT_RESET:
		io_out_word(io_port, BIN_DIS(value, UHCI_PORT_SC_RESET));
		break;
	default:
		return USB_SETUP_STALLED;
	}
	return USB_SETUP_SUCCESS;
}

uint32_t uhci_get_hub_status(UsbHub *hub) {
	return 0b00;
}

uint32_t uhci_get_port_status(UsbHub *hub, uint8_t port) {
	UsbHcd	*hcd			  = hub->hcd;
	Uhci	*uhci			  = (Uhci *)hcd->device->private_data;
	uint32_t io_port		  = uhci->io_base + UHCI_PORTSC1 + port * 2;
	uint16_t uhci_port_status = io_in_word(io_port);
	uint16_t usb_port_status;

	usb_port_status = (uhci_port_status & UHCI_PORT_SC_CONNECTED) |
					  (uhci_port_status & UHCI_PORT_SC_ENABLE) >> 1 |
					  (uhci_port_status & UHCI_PORT_SC_SUSPEND) >> 10 |
					  (uhci_port_status & UHCI_PORT_SC_RESET) >> 5 |
					  USB_PORT_STAT_POWER |
					  (uhci_port_status & UHCI_PORT_SC_LOWSPEED);
	return usb_port_status;
}

UsbSetupStatus uhci_set_port_feature(
	UsbHub *hub, uint8_t port, uint16_t feature) {
	UsbHcd	*hcd	 = hub->hcd;
	Uhci	*uhci	 = (Uhci *)hcd->device->private_data;
	uint32_t io_port = uhci->io_base + UHCI_PORTSC1 + port * 2;
	uint16_t value	 = io_in_word(io_port);
	switch (feature) {
	case HUB_FEAT_PORT_RESET:
		uhci_port_reset(uhci, port);
		break;
	case HUB_FEAT_PORT_SUSPEND:
		io_out_word(io_port, BIN_EN(value, UHCI_PORT_SC_SUSPEND));
		break;
	case HUB_FEAT_PORT_POWER:
		break;
	default:
		return USB_SETUP_STALLED;
	}
	return USB_SETUP_SUCCESS;
}

void uhci_reset(Uhci *uhci) {
	// io_out16(uhci->io_base+UHCI_REG_USBINTR, 0); //禁用UHCI的所有中断
	io_out_word(
		uhci->io_base + UHCI_REG_CMD, UHCI_CMD_GLBRESET | UHCI_CMD_HCRESET);
	delay_ms(&uhci->timer, 10);
	io_out_word(uhci->io_base + UHCI_REG_CMD, 0);

	for (int i = 2; i < uhci->port_cnt; i++) // 逐个检测是否有效
	{
		uint16_t value = io_in_word(UHCI_PORTSC1 + i * 2);
		if (((value & 0x80) == 0) || value == 0xffff) {
			uhci->port_cnt = i;
			break;
		}
	}
}

void uhci_port_reset(Uhci *uhci, int port) {
	// 重置
	uint32_t io_port = uhci->io_base + UHCI_PORTSC1 + port * 2;

	uint16_t value = io_in_word(io_port);
	io_out_word(io_port, BIN_EN(value, UHCI_PORT_SC_RESET));

	delay_ms(&uhci->timer, 10);

	value = io_in_word(io_port);
	io_out_word(io_port, BIN_DIS(value, UHCI_PORT_SC_RESET));
	do {
		value = io_in_word(io_port);
	} while (BIN_IS_EN(value, UHCI_PORT_SC_RESET));

	delay_ms(&uhci->timer, 10);

	if (value & UHCI_PORT_SC_CONNECTED) {
		io_out_word(io_port, BIN_EN(value, UHCI_PORT_SC_ENABLE));
	}
	// 使能
	value = io_in_word(io_port);
	io_out_word(
		io_port, BIN_EN(value, UHCI_PORT_SC_CONN_CHG | UHCI_PORT_SC_EN_CHG));
	io_in_word(io_port);

	delay_ms(&uhci->timer, 10);
}

void uhci_port_init(UsbHub *hub, UsbHcd *hcd, int port) {
	Uhci	   *uhci	 = (Uhci *)hcd->device->private_data;
	uint32_t	io_port	 = uhci->io_base + UHCI_PORTSC1 + port * 2;
	UsbHcdPort *hcd_port = &hcd->ports[port];

	// 获取端口信息
	uint32_t port_status = io_in_word(io_port);

	// 重置端口，注册设备
	if (BIN_IS_EN(port_status, UHCI_PORT_SC_CONNECTED)) {
		UsbDeviceSpeed speed = BIN_IS_EN(port_status, UHCI_PORT_SC_LOWSPEED)
								 ? USB_SPEED_LOW
								 : USB_SPEED_FULL;
		printk("[UHCI]port %d connected.\n", port);

		uhci_port_reset(uhci, port);

		usb_probe_device(hcd, hub, speed);
	}

	// 输出端口信息
	port_status			= io_in_word(io_port);
	hcd_port->connected = BIN_IS_EN(port_status, UHCI_PORT_SC_CONNECTED);
	hcd_port->enable	= BIN_IS_EN(port_status, UHCI_PORT_SC_ENABLE);
	hcd_port->suspend	= BIN_IS_EN(port_status, UHCI_PORT_SC_SUSPEND);

	printk(
		"[UHCI]suspend: %d, enable: %d, connected: %d,", hcd_port->suspend,
		hcd_port->enable, hcd_port->connected);
	printk(
		"speed: %s\n", BIN_IS_EN(port_status, UHCI_PORT_SC_LOWSPEED)
						   ? "LowSpeed"
						   : "FullSpeed");
}

DriverResult uhci_init(void *_device) {
	PhysicalDevice *device = _device;
	Uhci		   *uhci   = device->private_data;

	timer_init(&uhci->timer);
	uhci_reset(uhci);
	uint16_t intr = io_in_word(uhci->io_base + UHCI_REG_USBINTR);
	intr |= UHCI_INTR_SPI | UHCI_INTR_IOC | UHCI_INTR_RESUME | UHCI_INTR_CRC;
	io_out_word(uhci->io_base + UHCI_REG_USBINTR, intr);

	return DRIVER_OK;
}

void uhci_probe_thread(void *arg) {
	Uhci *uhci = (Uhci *)arg;

	UsbHub *hub		= kmalloc(sizeof(UsbHub));
	hub->usb_device = NULL;
	hub->hcd		= uhci->hcd;
	hub->ops		= &uhci_root_hub_ops;

	struct UsbHubDescriptor *desc = kmalloc(sizeof(struct UsbHubDescriptor));
	hub->desc					  = desc;
	desc->bLength				  = 9;
	desc->bDescriptorType		  = USB_DESC_TYPE_HUB;
	desc->bNbrPorts				  = uhci->port_cnt;
	desc->wHubCharacteristics = HOST2LE_WORD(0x0009); // 无电源开关，单独供电
	desc->bPwrOn2PwrGood	  = 0;
	desc->bHubContrCurrent	  = 0;
	memset(&desc->DeviceRemovable, 0xff, 8); // 都是可移除的
	memset(&desc->PortPwrCtrlMask, 0xff, 8); // 都是电源控制的

	for (int i = 0; i < uhci->port_cnt; i++) {
		uhci_port_init(hub, uhci->hcd, i);
	}
}

void uhci_probe(Uhci *uhci) {
	// 通过独立线程初始化，避免usb初始化长时间的等待导致系统阻塞
	thread_start(
		"UHCI Probe", THREAD_DEFAULT_PRIO, uhci_probe_thread, uhci, NULL);
}

DriverResult uhci_start(void *_device) {
	PhysicalDevice *device = _device;
	Uhci		   *uhci   = device->private_data;
	uhci->fl.frames_vir	   = (uint32_t *)kernel_alloc_pages(1);
	uhci->fl.frames_phy	   = (uint32_t *)vir2phy((uint32_t)uhci->fl.frames_vir);
	uhci_skel_init(uhci);

	pci_device_write16(uhci->device, UHCI_PCI_REG_LEGSUP, 0x2000);
	pci_enable_bus_mastering(uhci->device);

	usb_legacy_support_disabled = true;

	io_out_word(uhci->io_base + UHCI_FRNUM, 0);
	io_out_dword(uhci->io_base + UHCI_FRBASEADD, (uint32_t)uhci->fl.frames_phy);

	uint16_t cmd = io_in_word(uhci->io_base + UHCI_REG_CMD);
	io_out_word(uhci->io_base + UHCI_REG_CMD, cmd | UHCI_CMD_RUN);

	uhci_probe(uhci);

	register_device_irq(
		&uhci->irq, device, uhci, uhci->device->irqline, uhci_handler,
		IRQ_MODE_SHARED);

	enable_device_irq(uhci->irq);

	return DRIVER_OK;
}

DriverResult uhci_pci_probe(
	PciDevice *pci_device, PhysicalDevice *physical_device) {
	uint32_t io_base = pci_device->common.bar[4].base_addr & 0xfffffff0;
	if (io_base == 0) { return DRIVER_ERROR_UNSUPPORT_DEVICE; }

	uint8_t port_cnt = (pci_device->common.bar[4].length - UHCI_PORTSC1) / 2;

	uint16_t status;
	for (int i = 0; i < port_cnt; i++) {
		status = io_in_word(io_base + UHCI_PORTSC1 + i * 2);
		if ((status & 0x80) == 0 || status == 0xffff) {
			port_cnt = i;
			break;
		}
	}

	DriverResult result;

	Uhci *uhci	   = kmalloc(sizeof(Uhci));
	uhci->device   = pci_device;
	uhci->io_base  = io_base;
	uhci->port_cnt = port_cnt;

	result = usb_create_hcd(
		&uhci->hcd, port_cnt, &uhci_hcd_ops, &uhci_logical_device_ops,
		physical_device, &uhci_device_driver);
	if (result != DRIVER_OK) {
		kfree(uhci);
		return result;
	}

	usb_legacy_support_disabled = false; // 发现USB控制器，让8042驱动等待

	LogicalDevice *logical_device = uhci->hcd->device;
	logical_device->private_data  = uhci;
	physical_device->private_data = uhci;

	register_physical_device(physical_device, &uhci_device_ops);

	return DRIVER_OK;
}

static __init void uhci_driver_entry(void) {
	register_device_driver(&x86_usb_hcd_driver, &uhci_device_driver);
	pci_register_driver(&x86_usb_hcd_driver, &uhci_pci_driver);
}

driver_initcall(uhci_driver_entry);