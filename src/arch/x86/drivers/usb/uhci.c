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
#include <drivers/pci.h>
#include <drivers/pit.h>
#include <drivers/usb/uhci.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/page.h>

#define UHCI_CLASSID	0x0c
#define UHCI_SUBCLASSID 0x03
#define UHCI_PROGIF		0x00

#define DRV_NAME "Universal Serial Bus(USB) Driver"
#define DEV_NAME "Universal Host Controller Interface(UHCI) Controller"

static status_t uhci_enter(driver_t *drv_obj);
static status_t uhci_exit(driver_t *drv_obj);

driver_func_t uhci_driver = {.driver_enter	= uhci_enter,
							 .driver_exit	= uhci_exit,
							 .driver_open	= NULL,
							 .driver_close	= NULL,
							 .driver_read	= NULL,
							 .driver_write	= NULL,
							 .driver_devctl = NULL};

typedef struct {
	struct pci_device	  *device;
	uint32_t			   io_base;
	struct uhci_frame_list fl;
	uint8_t				   port_cnt;

	struct uhci_skel *skel;
} device_extension_t;

void uhci_skel_init(device_extension_t *devext) {
	int i, j, step;

	devext->skel = kmalloc(sizeof(struct uhci_skel));

	memset32(devext->fl.frames_vir, 1, 1024);

	for (i = 0; i < 12; i++) {
		step = 1 << i;
		for (j = 0; j < 1024 / step; j += step) {
			struct uhci_qh *qh;
			if ((devext->fl.frames_vir[j] & 0x01) == 0) {
				qh = (struct uhci_qh *)(devext->fl.frames_vir[j] & 0xfffffff0);
				while ((qh->qh_link & 0x01) == 0) {
					qh = (struct uhci_qh *)(qh->qh_link & 0xfffffff0);
				}
				qh->qh_link = (((uint32_t)kmalloc(sizeof(struct uhci_qh))) & 0xfffffff0) | (1 << 1) | 0;
				qh			= (struct uhci_qh *)(qh->qh_link & 0xfffffff0);
			} else {
				qh						 = kmalloc(sizeof(struct uhci_qh));
				devext->fl.frames_vir[j] = (((uint32_t)qh) & 0xfffffff0) | (1 << 1) | 0;
			}
			qh->qe_link = (((uint32_t)&devext->skel->qh[i]) & 0xfffffff0) | (1 << 1) | 1;
			qh->qh_link = 1;
		}
	}
}

void uhci_skel_add_td(device_extension_t *devext, struct uhci_td *td, enum uhci_gap gap) {
	struct uhci_td *tmp = (struct uhci_td *)(((uint32_t)devext->skel->qh[gap].qe_link) & 0xfffffff0);
	while ((((uint32_t)tmp->link) & 0x01) == 0) {
		tmp = (struct uhci_td *)(((uint32_t)tmp->link) & 0xfffffff0);
	}
	tmp->link = ((uint32_t)td & 0xfffffff0) | (0 << 1) | 1;
}

int uhci_skel_del_td(device_extension_t *devext, struct uhci_td *td, enum uhci_gap gap) {
	struct uhci_td *tmp = (struct uhci_td *)(((uint32_t)devext->skel->qh[gap].qe_link) & 0xfffffff0);
	while ((tmp->link & 0x01) != 1 && (tmp->link & 0xfffffff0) != (uint32_t)td) {
		tmp = (struct uhci_td *)(((uint32_t)tmp->link) & 0xfffffff0);
	}
	if ((tmp->link & 0xfffffff0) == (uint32_t)td) {
		tmp->link = td->link;
		kfree(td);
		return 0;
	}
	return -1;
}

void uhci_handler(int irq) {
	return;
}

void uhci_print_status(device_extension_t *devext) {
	uint16_t status = io_in16(devext->io_base + UHCI_REG_STS);
	printk("\n[UHCI]Status:\n");
	if (status & 0x20) { printk("[UHCI]HCHalted.\n"); }
	if (status & 0x10) { printk("[UHCI]HC Process Error.\n"); }
	if (status & 0x08) { printk("[UHCI]Host System Error.\n"); }
	if (status & 0x04) { printk("[UHCI]Resume Detect.\n"); }
	printk("[UHCI]Error Int:%d\n", status & 0x02);
	printk("[UHCI]Interrupt:%d\n", status & 0x01);
}

void uhci_reset(device_extension_t *devext) {
	int i;

	// io_out16(devext->io_base+UHCI_REG_USBINTR, 0); //禁用UHCI的所有中断
	io_out16(devext->io_base + UHCI_REG_CMD, UHCI_CMD_GLBRESET);
	delay(50 / 10); // 至少50ms
	io_out16(devext->io_base + UHCI_REG_CMD, 0);

	devext->port_cnt = (devext->device->bar[4].length - UHCI_PORTSC1) / 2; // 有多少接口
	for (i = 2; i < devext->port_cnt; i++)								   // 逐个检测是否有效
	{
		uint16_t value = io_in16(UHCI_PORTSC1 + i * 2);
		if (((value & 0x80) == 0) || value == 0xffff) {
			devext->port_cnt = i;
			break;
		}
	}
}

static status_t uhci_enter(driver_t *drv_obj) {
	device_t		   *devobj;
	device_extension_t *devext;

	device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_USB, &devobj);
	devext = devobj->device_extension;

	devext->device = pci_get_device_ByClassFull(UHCI_CLASSID, UHCI_SUBCLASSID, UHCI_PROGIF);
	if (devext->device == NULL) {
		printk(COLOR_YELLOW "\n[UHCI]Cannot find UHCI controller!\n");
		device_delete(devobj);
		return NODEV;
	}

	devext->io_base = devext->device->bar[4].base_addr & 0xfffffff0;

	pci_enable_bus_mastering(devext->device);
	pci_enable_io_space(devext->device);

	uhci_reset(devext);
	uint16_t intr = io_in16(devext->io_base + UHCI_REG_USBINTR);
	io_out16(devext->io_base + UHCI_REG_USBINTR,
			 intr | UHCI_INTR_SPI | UHCI_INTR_IOC | UHCI_INTR_RESUME | UHCI_INTR_CRC);
	devext->fl.frames_vir = (uint32_t *)kernel_alloc_pages(1);
	devext->fl.frames_phy = (uint32_t *)vir2phy((uint32_t)devext->fl.frames_vir);
	uhci_skel_init(devext);

	io_out16(devext->io_base + UHCI_FRNUM, 0);
	io_out16(devext->io_base + UHCI_FRBASEADD, (uint32_t)devext->fl.frames_phy);

	uint16_t cmd = io_in16(devext->io_base + UHCI_REG_CMD);
	io_out16(devext->io_base + UHCI_REG_CMD, cmd | UHCI_CMD_RUN);

	return SUCCUESS;
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