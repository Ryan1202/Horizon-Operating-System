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
#include <kernel/thread.h>
#include <objects/object.h>
#include <result.h>
#include <string.h>

LIST_HEAD(new_bus_lh);
LIST_HEAD(bus_check_lh);
LIST_HEAD(new_device_lh);
SPINLOCK(new_bus_lock);
SPINLOCK(device_list_lock);

static size_t driver_init_thread_count;

Driver core_driver = {
	.short_name = STRING_INIT("CoreDriver"),
	.state		= DRIVER_STATE_UNREGISTERED,
};

static void queue_new_device(PhysicalDevice *device) {
	int flags = spin_lock_irqsave(&device_list_lock);
	if (!list_in_list(&device->new_device_list))
		list_add_tail(&device->new_device_list, &new_device_lh);
	spin_unlock_irqrestore(&device_list_lock, flags);
}

static Bus *take_new_bus(void) {
	int	 flags = spin_lock_irqsave(&new_bus_lock);
	Bus *bus	= list_first_owner_or_null(&new_bus_lh, Bus, new_bus_list);
	if (bus != NULL) list_del(&bus->new_bus_list);
	spin_unlock_irqrestore(&new_bus_lock, flags);
	return bus;
}

static PhysicalDevice *take_new_device(void) {
	int flags = spin_lock_irqsave(&device_list_lock);
	PhysicalDevice *device = list_first_owner_or_null(
		&new_device_lh, PhysicalDevice, new_device_list);
	if (device != NULL) list_del(&device->new_device_list);
	spin_unlock_irqrestore(&device_list_lock, flags);
	return device;
}

static bool has_new_bus(void) {
	int	 flags	  = spin_lock_irqsave(&new_bus_lock);
	bool has_bus = !list_empty(&new_bus_lh);
	spin_unlock_irqrestore(&new_bus_lock, flags);
	return has_bus;
}

static bool has_new_device(void) {
	int	 flags		 = spin_lock_irqsave(&device_list_lock);
	bool has_device = !list_empty(&new_device_lh);
	spin_unlock_irqrestore(&device_list_lock, flags);
	return has_device;
}

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
			queue_new_device(phy);
		}
	}
	while ((phy = take_new_device()) != NULL) {
		if (phy->ops == NULL) { // 没有绑定驱动
			continue;
		}
		if (phy->state == DEVICE_STATE_UNINIT) init_physical_device(phy);
		if (phy->state == DEVICE_STATE_READY) start_physical_device(phy);
		list_for_each_owner (
			logi, &phy->logical_device_lh, logical_device_list) {
			if (logi->state == DEVICE_STATE_UNINIT) init_logical_device(logi);
			if (logi->state == DEVICE_STATE_READY) start_logical_device(logi);
		}
	}
}

static void start_device(void *arg) {
	PhysicalDevice *phy = arg;
	LogicalDevice  *logi;

	if (phy->state == DEVICE_STATE_UNINIT) {
		DriverResult result;
		do {
			result = init_physical_device(phy);
			if (result == DRIVER_ERROR_WAITING) try_yield();
		} while (result == DRIVER_ERROR_WAITING);
	}
	if (phy->state == DEVICE_STATE_READY) start_physical_device(phy);
	list_for_each_owner (logi, &phy->logical_device_lh, logical_device_list) {
		if (logi->state == DEVICE_STATE_UNINIT) init_logical_device(logi);
		if (logi->state == DEVICE_STATE_READY) start_logical_device(logi);
	}
}

static void start_device_thread(void *arg) {
	start_device(arg);

	__atomic_fetch_sub(&driver_init_thread_count, 1, __ATOMIC_RELEASE);
}

static DriverResult start_device_async(PhysicalDevice *device) {
	struct Thread *thread =
		thread_create("Start Device", start_device_thread, device);
	if (thread == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;

	__atomic_fetch_add(&driver_init_thread_count, 1, __ATOMIC_RELAXED);
	if (!thread_run(thread)) {
		__atomic_fetch_sub(&driver_init_thread_count, 1, __ATOMIC_RELEASE);
		return DRIVER_ERROR_OTHER;
	}

	return DRIVER_OK;
}

static DriverResult start_devices(void) {
	DriverResult result = DRIVER_OK;

	for (;;) {
		Bus *bus;
		while ((bus = take_new_bus()) != NULL) {
			if (bus->ops->scan_bus != NULL)
				bus->ops->scan_bus(bus->bus_driver, bus);
			if (bus->ops->probe_device != NULL)
				bus->ops->probe_device(bus->bus_driver, bus);

			PhysicalDevice *phy, *phy_next;
			list_for_each_owner_safe (
				phy, phy_next, &bus->device_lh, device_list) {
				if (phy->state != DEVICE_STATE_UNINIT) continue;
				queue_new_device(phy);
			}
		}

		PhysicalDevice *phy;
		while ((phy = take_new_device()) != NULL) {
			if (phy->ops == NULL) { // 没有绑定驱动
				continue;
			}

			DriverResult start_result = start_device_async(phy);
			if (start_result != DRIVER_OK) {
				if (result == DRIVER_OK) result = start_result;
				start_device(phy);
			}
		}

		if (__atomic_load_n(
				&driver_init_thread_count, __ATOMIC_ACQUIRE) == 0 &&
			!has_new_bus() && !has_new_device())
			break;

		try_yield();
	}

	return result;
}

PeriodicTask driver_periodic_task = {
	.func = device_detect,
	.arg  = NULL,
};

DriverResult driver_start_all(void) {
	DriverResult result = start_devices();

	periodic_task_add(&driver_periodic_task);

	return result;
}
