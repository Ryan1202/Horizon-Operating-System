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
#include "driver/bus_dm.h"
#include "driver/timer_dm.h"
#include "kernel/driver_interface.h"
#include "kernel/list.h"
#include "kernel/thread.h"
#include "objects/object.h"
#include <bits.h>
#include <drivers/bus/pci/pci.h>
#include <drivers/bus/usb.h>
#include <drivers/pit.h>
#include <drivers/usb/hcd.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/platform.h>
#include <stdint.h>
#include <string.h>

#define UHCI_CLASSID	0x0c
#define UHCI_SUBCLASSID 0x03
#define UHCI_PROGIF		0x00

#define DRV_NAME "Universal Serial Bus(USB) Driver"
#define DEV_NAME "Universal Host Controller Interface(UHCI)"

void		 uhci_register(Driver *driver);
DriverResult uhci_init(Device *device);
DriverResult uhci_start(Device *device);
DriverResult uhci_pci_probe(PciDevice *pci_device);

extern UsbHcdOps uhci_interface;

DeviceDriverOps uhci_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
PciDriverOps uhci_pci_driver_ops = {
	.probe = uhci_pci_probe,
};
DeviceOps uhci_device_ops = {
	.init	 = uhci_init,
	.start	 = uhci_start,
	.destroy = NULL,
	.status	 = NULL,
	.stop	 = NULL,
};

DeviceDriver uhci_device_driver = {
	.name	  = STRING_INIT("uhci"),
	.type	  = DEVICE_TYPE_BUS_CONTROLLER,
	.priority = DRIVER_PRIORITY_BASIC,
	.ops	  = &uhci_device_driver_ops,
};
PciDriver uhci_pci_driver = {
	.driver				   = &usb_driver,
	.device_driver		   = &uhci_device_driver,
	.find_type			   = FIND_BY_CLASSCODE_SUBCLASS_PROGIF,
	.class_subclass_progif = {UHCI_CLASSID, UHCI_SUBCLASSID, UHCI_PROGIF},
	.ops				   = &uhci_pci_driver_ops,
};
BusControllerDevice uhci_bus_controller_device = {
	.short_name			= STRING_INIT("UHCI Controller"),
	.device				= NULL, // 由probe函数设置
	.bus_driver			= &usb_bus_driver,
	.bus_controller_ops = NULL,
};
const Device uhci_device_template = {
	.name			   = STRING_INIT("UHCI Controller"),
	.device_driver	   = &uhci_device_driver,
	.ops			   = &uhci_device_ops,
	.private_data_size = sizeof(Uhci),
};
HciInit uhci_hci_init = {
	.init = uhci_register,
};
// const UsbHciDevice uhci_hci_device_template = {
// 	.head_size = 0,
// 	.tail_size = 0,
// };

void uhci_port_reset(Uhci *devext, int port);
void uhci_port_init(UsbHcd *hcd, int port);

void uhci_handler(int irq) {
	return;
}

void uhci_print_status(Uhci *devext) {
	uint16_t status = io_in_word(devext->io_base + UHCI_REG_STS);
	printk("\n[UHCI]Status:\n");
	if (status & 0x20) { printk("[UHCI]HC Halted.\n"); }
	if (status & 0x10) { printk("[UHCI]HC Process Error.\n"); }
	if (status & 0x08) { printk("[UHCI]Host System Error.\n"); }
	if (status & 0x04) { printk("[UHCI]Resume Detect.\n"); }
	printk("[UHCI]Error Int:%d\n", status & 0x02);
	printk("[UHCI]Interrupt:%d\n", status & 0x01);
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

	delay_ms(&uhci->timer, 50);

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

void uhci_port_init(UsbHcd *hcd, int port) {
	Uhci	   *devext	 = (Uhci *)hcd->device->private_data;
	uint32_t	io_port	 = devext->io_base + UHCI_PORTSC1 + port * 2;
	UsbHcdPort *hcd_port = &hcd->ports[port];

	// 获取端口信息
	uint32_t port_status = io_in_word(io_port);

	// 重置端口，注册设备
	if (BIN_IS_EN(port_status, UHCI_PORT_SC_CONNECTED)) {
		printk("[UHCI]port %d connected.\n", port);
		UsbDevice *usb_device = usb_create_device(
			BIN_IS_EN(port_status, UHCI_PORT_SC_LOWSPEED) ? USB_SPEED_LOW
														  : USB_SPEED_FULL,
			0);

		uhci_port_reset(devext, port);

		usb_init_device(hcd, usb_device);
	}

	// 输出端口信息
	port_status			= io_in_word(io_port);
	hcd_port->connected = BIN_IS_EN(port_status, UHCI_PORT_SC_CONNECTED);
	hcd_port->enable	= BIN_IS_EN(port_status, UHCI_PORT_SC_ENABLE);
	hcd_port->suspend	= BIN_IS_EN(port_status, UHCI_PORT_SC_SUSPEND);

	printk("[UHCI]port %d reset. status: %04x\n", port, port_status);
	printk(
		"[UHCI]suspend: %d, enable: %d, connected: %d,", hcd_port->suspend,
		hcd_port->enable, hcd_port->connected);
	printk(
		"speed: %s\n", BIN_IS_EN(port_status, UHCI_PORT_SC_LOWSPEED)
						   ? "LowSpeed"
						   : "FullSpeed");
}

DriverResult uhci_init(Device *device) {
	Uhci *uhci = device->private_data;

	timer_init(&uhci->timer);
	uhci_reset(uhci);
	uint16_t intr = io_in_word(uhci->io_base + UHCI_REG_USBINTR);
	intr |= UHCI_INTR_SPI | UHCI_INTR_IOC | UHCI_INTR_RESUME | UHCI_INTR_CRC;
	io_out_word(uhci->io_base + UHCI_REG_USBINTR, intr);

	return DRIVER_RESULT_OK;
}

DriverResult uhci_start(Device *device) {
	Uhci *uhci			= device->private_data;
	uhci->fl.frames_vir = (uint32_t *)kernel_alloc_pages(1);
	uhci->fl.frames_phy = (uint32_t *)vir2phy((uint32_t)uhci->fl.frames_vir);
	uhci_skel_init(uhci);

	pci_device_write16(uhci->device, UHCI_PCI_REG_LEGSUP, 0x2000);
	pci_enable_bus_mastering(uhci->device);

	io_out_word(uhci->io_base + UHCI_FRNUM, 0);
	io_out_dword(uhci->io_base + UHCI_FRBASEADD, (uint32_t)uhci->fl.frames_phy);

	uint16_t cmd = io_in_word(uhci->io_base + UHCI_REG_CMD);
	io_out_word(uhci->io_base + UHCI_REG_CMD, cmd | UHCI_CMD_RUN);

	for (int i = 0; i < uhci->port_cnt; i++) {
		uhci_port_init(uhci->hcd, i);
	}

	return DRIVER_RESULT_OK;
}

DriverResult uhci_pci_probe(PciDevice *pci_device) {
	uint32_t io_base = pci_device->common.bar[4].base_addr & 0xfffffff0;
	if (io_base == 0) { return DRIVER_RESULT_UNSUPPORT_DEVICE; }

	Device *device = kmalloc_from_template(uhci_device_template);
	device->bus	   = pci_device->bus;

	ObjectAttr attr = driver_object_attr;
	register_bus_controller_device(
		&uhci_device_driver, &usb_bus_driver, device,
		&uhci_bus_controller_device, &attr);
	Uhci *uhci	   = device->private_data;
	uhci->device   = pci_device;
	uhci->io_base  = io_base;
	uhci->port_cnt = (pci_device->common.bar[4].length - UHCI_PORTSC1) / 2;

	UsbHcd *hcd =
		usb_hcd_register(device, DEV_NAME, uhci->port_cnt, &uhci_interface);
	uhci->hcd = hcd;

	return DRIVER_RESULT_OK;
}

void uhci_register(Driver *driver) {
	register_device_driver(driver, &uhci_device_driver);
	pci_register_driver(driver, &uhci_pci_driver);
}

static __init void uhci_driver_entry(void) {
	list_add_tail(&uhci_hci_init.list, &hci_lh);
}

driver_initcall(uhci_driver_entry);