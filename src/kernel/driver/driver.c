/**
 * @file driver.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 驱动接口
 * @version 0.3
 * @date 2022-07-20
 */
#include "kernel/periodic_task.h"
#include <fs/fs.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <kernel/sync.h>
#include <kernel/thread.h>
#include <kernel/wait_queue.h>
#include <objects/object.h>
#include <result.h>

LIST_HEAD(new_bus_lh);
LIST_HEAD(bus_check_lh);
LIST_HEAD(new_device_lh);
SPINLOCK(device_list_lock);

Driver core_driver = {
	.short_name = STRING_INIT("CoreDriver"),
	.state		= DRIVER_STATE_UNREGISTERED,
};

void print_driver_result(
	DriverResult result, char *file, int line, char *func_with_args) {
	if (result == DRIVER_OK) return;
	printk("[At file %s line%d: %s]", file, line, func_with_args);
	switch (result) {
		RESULT_CASE_PRINT(DRIVER_OK)
		RESULT_CASE_PRINT(DRIVER_ERROR_TIMEOUT)
		RESULT_CASE_PRINT(DRIVER_ERROR_CONFLICT)
		RESULT_CASE_PRINT(DRIVER_ERROR_NO_OPS)
		RESULT_CASE_PRINT(DRIVER_ERROR_OBJECT)
		RESULT_CASE_PRINT(DRIVER_ERROR_INCOMPLETABLE_OPS)
		RESULT_CASE_PRINT(DRIVER_ERROR_INVALID_IRQ_NUMBER)
		RESULT_CASE_PRINT(DRIVER_ERROR_OUT_OF_MEMORY)
		RESULT_CASE_PRINT(DRIVER_ERROR_ALREADY_EXIST)
		RESULT_CASE_PRINT(DRIVER_ERROR_NOT_EXIST)
		RESULT_CASE_PRINT(DRIVER_ERROR_NULL_POINTER)
		RESULT_CASE_PRINT(DRIVER_ERROR_UNSUPPORT_DEVICE)
		RESULT_CASE_PRINT(DRIVER_ERROR_UNSUPPORT_FEATURE)
		RESULT_CASE_PRINT(DRIVER_ERROR_INVALID_TYPE)
		RESULT_CASE_PRINT(DRIVER_ERROR_WAITING)
		RESULT_CASE_PRINT(DRIVER_ERROR_MEMORY_FREE)
		RESULT_CASE_PRINT(DRIVER_ERROR_BUSY)
		RESULT_CASE_PRINT(DRIVER_ERROR_EXCEED_MAX_SIZE)
		RESULT_CASE_PRINT(DRIVER_ERROR_OTHER)
	}
}

DriverResult register_driver(Driver *driver) {
	driver->state = DRIVER_STATE_REGISTERED;
	list_init(&driver->device_driver_lh);
	list_init(&driver->remapped_memory_lh);

	return DRIVER_OK;
}

DriverResult unregister_driver(Driver *driver) {
	return DRIVER_OK;
}

void device_detect(void *arg) {
	Bus			   *bus, *next;
	PhysicalDevice *phy, *phy_next;
	LogicalDevice  *logi;
	list_for_each_owner_safe (bus, next, &bus_check_lh, bus_check_list) {
		if (bus->ops->probe_device != NULL)
			bus->ops->probe_device(bus->bus_driver, bus);
		list_for_each_owner_safe (phy, phy_next, &bus->device_lh, device_list) {
			if (phy->state != DEVICE_STATE_UNINIT) continue;
			spin_lock(&device_list_lock);
			list_add_tail(&phy->new_device_list, &new_device_lh);
			spin_unlock(&device_list_lock);
		}
	}
	while (!list_empty(&new_device_lh)) {
		spin_lock(&device_list_lock);
		phy = list_first_owner_or_null(
			&new_device_lh, PhysicalDevice, new_device_list);
		spin_unlock(&device_list_lock);
		if (phy == NULL) {
			schedule();
			continue;
		}
		if (phy->ops == NULL) { // 没有绑定驱动
			spin_lock(&device_list_lock);
			list_del(&phy->new_device_list);
			spin_unlock(&device_list_lock);
			continue;
		}
		if (phy->state == DEVICE_STATE_UNINIT) init_physical_device(phy);
		if (phy->state == DEVICE_STATE_READY) start_physical_device(phy);
		list_for_each_owner (
			logi, &phy->logical_device_lh, logical_device_list) {
			if (logi->state == DEVICE_STATE_UNINIT) init_logical_device(logi);
			if (logi->state == DEVICE_STATE_READY) start_logical_device(logi);
		}
		spin_lock(&device_list_lock);
		list_del(&phy->new_device_list);
		spin_unlock(&device_list_lock);
	}
}

void start_devices(void *arg) {
	Bus			   *bus, *next;
	PhysicalDevice *phy, *phy_next;
	LogicalDevice  *logi;
	while (!(list_empty(&new_bus_lh) && list_empty(&new_device_lh))) {
		list_for_each_owner_safe (bus, next, &new_bus_lh, new_bus_list) {
			if (bus->ops->scan_bus != NULL)
				bus->ops->scan_bus(bus->bus_driver, bus);
			if (bus->ops->probe_device != NULL)
				bus->ops->probe_device(bus->bus_driver, bus);
			list_for_each_owner_safe (
				phy, phy_next, &bus->device_lh, device_list) {
				if (phy->state != DEVICE_STATE_UNINIT) continue;
				spin_lock(&device_list_lock);
				list_add_tail(&phy->new_device_list, &new_device_lh);
				spin_unlock(&device_list_lock);
			}
			list_del(&bus->new_bus_list);
		}
		while (!list_empty(&new_device_lh)) {
			spin_lock(&device_list_lock);
			phy = list_first_owner_or_null(
				&new_device_lh, PhysicalDevice, new_device_list);
			spin_unlock(&device_list_lock);
			if (phy == NULL) {
				schedule();
				continue;
			}
			if (phy->ops == NULL) { // 没有绑定驱动
				spin_lock(&device_list_lock);
				list_del(&phy->new_device_list);
				spin_unlock(&device_list_lock);
				continue;
			}
			if (phy->state == DEVICE_STATE_UNINIT) init_physical_device(phy);
			if (phy->state == DEVICE_STATE_READY) start_physical_device(phy);
			list_for_each_owner (
				logi, &phy->logical_device_lh, logical_device_list) {
				if (logi->state == DEVICE_STATE_UNINIT)
					init_logical_device(logi);
				if (logi->state == DEVICE_STATE_READY)
					start_logical_device(logi);
			}
			spin_lock(&device_list_lock);
			list_del(&phy->new_device_list);
			spin_unlock(&device_list_lock);
		}
	}
}

PeriodicTask driver_periodic_task = {
	.func = device_detect,
	.arg  = NULL,
};

DriverResult driver_start_all(void) {
	thread_start(
		"Start Devices", THREAD_DEFAULT_PRIO, start_devices, NULL, NULL);
	while (!(list_empty(&new_bus_lh) && list_empty(&new_device_lh))) {
		schedule();
	}

	periodic_task_add(&driver_periodic_task);

	return DRIVER_OK;
}
