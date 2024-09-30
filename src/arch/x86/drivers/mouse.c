/**
 * @file mouse.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 鼠标驱动
 * @version 0.2
 * @date 2021-06
 */
#include <drivers/8042.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/fifo.h>
#include <kernel/func.h>
#include <kernel/initcall.h>

struct fifo mouse_fifo;

static status_t mouse_enter(driver_t *drv_obj);
static status_t mouse_devctl(struct _device_s *dev, uint32_t func_num, uint32_t value);
static status_t mouse_exit(driver_t *drv_obj);

#define MOUSE_IRQ 12

driver_func_t mouse_driver = {
	.driver_enter  = mouse_enter,
	.driver_exit   = mouse_exit,
	.driver_open   = NULL,
	.driver_close  = NULL,
	.driver_read   = NULL,
	.driver_write  = NULL,
	.driver_devctl = mouse_devctl,
};

#define DRV_NAME "General PS/2 Driver(Mouse)"
#define DEV_NAME "mouse"

void mouse_handler(device_t *devobj, int irq) {
	fifo_put(&mouse_fifo, i8042_read_data());
}

static status_t mouse_enter(driver_t *drv_obj) {
	int		 *mouse_buf = kmalloc(128 * sizeof(int));
	device_t *devobj;

	device_create(drv_obj, 0, DEV_NAME, DEV_MOUSE, &devobj);

	fifo_init(&mouse_fifo, 128, mouse_buf);

	i8042_send_cmd(0xd4);
	i8042_write_data(0xf4);

	device_register_irq(devobj, MOUSE_IRQ, mouse_handler);
	return SUCCUESS;
}

static status_t mouse_devctl(struct _device_s *dev, uint32_t func_num, uint32_t value) {
	switch (func_num) {
	case 1:
		(*(uint32_t *)value) = (uint32_t)&mouse_fifo;
		break;

	default:
		break;
	}
	return SUCCUESS;
}

static status_t mouse_exit(driver_t *drv_obj) {
	device_t *devobj, *next;
	// device_extension_t *ext;
	list_for_each_owner_safe (devobj, next, &drv_obj->device_list, list) {
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

static __init void mouse_driver_entry(void) {
	if (driver_create(mouse_driver, DRV_NAME) < 0) {
		printk(COLOR_RED "[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(mouse_driver_entry);