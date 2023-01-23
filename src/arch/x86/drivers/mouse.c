/**
 * @file mouse.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PS/2鼠标驱动
 * @version 0.1
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
static status_t mouse_exit(driver_t *drv_obj);

#define MOUSE_IRQ 12

driver_func_t mouse_driver = {
    .driver_enter  = mouse_enter,
    .driver_exit   = mouse_exit,
    .driver_open   = NULL,
    .driver_close  = NULL,
    .driver_read   = NULL,
    .driver_write  = NULL,
    .driver_devctl = NULL
};

#define DRV_NAME "General PS/2 Driver(Mouse)"
#define DEV_NAME "PS/2 Mouse"

typedef struct
{
    int x, y;
    int old_x, old_y;
    int lbtn, mbtn, rbtn; // 左键 中键 右键
} device_extension_t;

static status_t mouse_enter(driver_t *drv_obj)
{
    device_t           *devobj;
    device_extension_t *devext;

    device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_MOUSE, &devobj);
    devext = devobj->device_extension;

    devext->x     = 0;
    devext->y     = 0;
    devext->old_x = 0;
    devext->old_y = 0;
    devext->lbtn  = 0;
    devext->mbtn  = 0;
    devext->rbtn  = 0;

    irq_enable(MOUSE_IRQ);
    return SUCCUESS;
}

static status_t mouse_exit(driver_t *drv_obj)
{
    device_t *devobj, *next;
    // device_extension_t *ext;
    list_for_each_owner_safe (devobj, next, &drv_obj->device_list, list) {
        device_delete(devobj);
    }
    string_del(&drv_obj->name);
    return SUCCUESS;
}

static __init void mouse_driver_entry(void)
{
    if (driver_create(mouse_driver, DRV_NAME) < 0) {
        printk(COLOR_RED "[driver] %s driver create failed!\n", __func__);
    }
}

driver_initcall(mouse_driver_entry);