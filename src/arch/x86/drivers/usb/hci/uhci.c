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
#include <bits.h>
#include <drivers/pci.h>
#include <drivers/pit.h>
#include <drivers/usb/hcd.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <stdint.h>
#include <string.h>

#define UHCI_CLASSID	0x0c
#define UHCI_SUBCLASSID 0x03
#define UHCI_PROGIF		0x00

#define DRV_NAME "Universal Serial Bus(USB) Driver"
#define DEV_NAME "Universal Host Controller Interface(UHCI)"

static status_t uhci_enter(driver_t *drv_obj);
static status_t uhci_exit(driver_t *drv_obj);

extern usb_hcd_interface_t uhci_interface;

driver_func_t uhci_driver = {
	.driver_enter  = uhci_enter,
	.driver_exit   = uhci_exit,
	.driver_open   = NULL,
	.driver_close  = NULL,
	.driver_read   = NULL,
	.driver_write  = NULL,
	.driver_devctl = NULL};

void uhci_port_reset(uhci_t *devext, int port);
void uhci_port_init(usb_hcd_t *hcd, int port);

void uhci_handler(int irq) {
	return;
}

void uhci_print_status(uhci_t *devext) {
	uint16_t status = io_in16(devext->io_base + UHCI_REG_STS);
	printk("\n[UHCI]Status:\n");
	if (status & 0x20) { printk("[UHCI]HCHalted.\n"); }
	if (status & 0x10) { printk("[UHCI]HC Process Error.\n"); }
	if (status & 0x08) { printk("[UHCI]Host System Error.\n"); }
	if (status & 0x04) { printk("[UHCI]Resume Detect.\n"); }
	printk("[UHCI]Error Int:%d\n", status & 0x02);
	printk("[UHCI]Interrupt:%d\n", status & 0x01);
}

void uhci_reset(uhci_t *devext) {
	int i;

	// io_out16(devext->io_base+UHCI_REG_USBINTR, 0); //禁用UHCI的所有中断
	io_out16(devext->io_base + UHCI_REG_CMD, UHCI_CMD_GLBRESET);
	delay(50 / 10); // 至少50ms
	io_out16(devext->io_base + UHCI_REG_CMD, 0);

	devext->port_cnt =
		(devext->device->bar[4].length - UHCI_PORTSC1) / 2; // 有多少接口
	for (i = 2; i < devext->port_cnt; i++) // 逐个检测是否有效
	{
		uint16_t value = io_in16(UHCI_PORTSC1 + i * 2);
		if (((value & 0x80) == 0) || value == 0xffff) {
			devext->port_cnt = i;
			break;
		}
	}
}

static status_t uhci_enter(driver_t *drv_obj) {
	device_t *devobj;
	uhci_t	 *devext;

	device_create(drv_obj, sizeof(uhci_t), DEV_NAME, DEV_USB, &devobj);
	devext = devobj->device_extension;

	devext->device =
		pci_get_device_ByClassFull(UHCI_CLASSID, UHCI_SUBCLASSID, UHCI_PROGIF);
	if (devext->device == NULL) {
		printk(COLOR_YELLOW "\n[UHCI]Cannot find UHCI controller!\n");
		device_delete(devobj);
		return NODEV;
	}
	printk("UHCI\n");

	devext->io_base = devext->device->bar[4].base_addr & 0xfffffff0;

	pci_enable_bus_mastering(devext->device);
	pci_enable_io_space(devext->device);

	uhci_reset(devext);
	uint16_t intr = io_in16(devext->io_base + UHCI_REG_USBINTR);
	io_out16(
		devext->io_base + UHCI_REG_USBINTR,
		intr | UHCI_INTR_SPI | UHCI_INTR_IOC | UHCI_INTR_RESUME |
			UHCI_INTR_CRC);
	devext->fl.frames_vir = (uint32_t *)kernel_alloc_pages(1);
	devext->fl.frames_phy =
		(uint32_t *)vir2phy((uint32_t)devext->fl.frames_vir);
	uhci_skel_init(devext);

	io_out16(devext->io_base + UHCI_FRNUM, 0);
	io_out32(devext->io_base + UHCI_FRBASEADD, (uint32_t)devext->fl.frames_phy);

	uint16_t cmd = io_in16(devext->io_base + UHCI_REG_CMD);
	io_out16(devext->io_base + UHCI_REG_CMD, cmd | UHCI_CMD_RUN);

	usb_hcd_t *hcd =
		usb_hcd_register(devobj, DEV_NAME, devext->port_cnt, &uhci_interface);

	int i;
	for (i = 0; i < devext->port_cnt; i++) {
		uhci_port_init(hcd, i);
	}

	return SUCCUESS;
}

void uhci_port_reset(uhci_t *devext, int port) {
	// 重置
	uint32_t io_port = devext->io_base + UHCI_PORTSC1 + port * 2;

	io_out16(io_port, UHCI_PORT_SC_RESET);
	delay(50 / 10);
	uint32_t value = io_in16(io_port);
	io_out16(io_port, BIN_DIS(value, UHCI_PORT_SC_RESET));
	do {
		value = io_in16(io_port);
	} while (BIN_IS_EN(value, UHCI_PORT_SC_RESET));
	delay(10 / 10);

	// 使能
	io_out16(
		io_port,
		UHCI_PORT_SC_CONN_CHG | UHCI_PORT_SC_EN_CHG | UHCI_PORT_SC_ENABLE);
	io_in16(io_port);
	delay(10 / 10);
}

void uhci_port_init(usb_hcd_t *hcd, int port) {
	uhci_t		   *devext	 = (uhci_t *)hcd->device->device_extension;
	uint32_t		io_port	 = devext->io_base + UHCI_PORTSC1 + port * 2;
	usb_hcd_port_t *hcd_port = &hcd->ports[port];

	// 获取端口信息
	uint32_t port_status = io_in16(io_port);

	// 重置端口，注册设备
	if (BIN_IS_EN(port_status, UHCI_PORT_SC_CONNECTED)) {
		usb_device_t *usb_device = usb_create_device(
			BIN_IS_EN(port_status, UHCI_PORT_SC_LOWSPEED) ? USB_SPEED_LOW
														  : USB_SPEED_FULL,
			0);

		uhci_port_reset(devext, port);

		usb_init_device(hcd, usb_device);
	}

	// 输出端口信息
	port_status			= io_in16(io_port);
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

static status_t uhci_exit(driver_t *drv_obj) {
	device_t *devobj, *next;
	list_for_each_owner_safe (devobj, next, &drv_obj->device_list, list) {
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

static __init void uhci_driver_entry(void) {
	if (driver_create(uhci_driver, DRV_NAME) < 0) {
		printk(COLOR_RED "[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(uhci_driver_entry);