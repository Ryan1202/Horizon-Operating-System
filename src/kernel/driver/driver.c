/**
 * @file driver.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 驱动接口
 * @version 0.3
 * @date 2022-07-20
 */
#include <fs/fs.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/wait_queue.h>
#include <objects/object.h>
#include <stdint.h>

LIST_HEAD(driver_list_head);

// --------new--------
#include <kernel/device_driver.h>
#include <kernel/driver_manager.h>
#include <kernel/sync.h>
#include <result.h>

LIST_HEAD(driver_lh);

Driver core_driver = {
	.short_name = STRING_INIT("CoreDriver"),
	.init		= NULL,
	.state		= DRIVER_STATE_UNINITED,
};

void print_driver_result(
	DriverResult result, char *file, int line, char *func_with_args) {
	if (result == DRIVER_RESULT_OK) return;
	printk("[At file %s line%d: %s]", file, line, func_with_args);
	switch (result) {
		RESULT_CASE_PRINT(DRIVER_RESULT_OK)
		RESULT_CASE_PRINT(DRIVER_RESULT_TIMEOUT)
		RESULT_CASE_PRINT(DRIVER_RESULT_DEVICE_DRIVER_CONFLICT)
		RESULT_CASE_PRINT(DRIVER_RESULT_NO_OPS)
		RESULT_CASE_PRINT(DRIVER_RESULT_INCOMPLETABLE_OPS)
		RESULT_CASE_PRINT(DRIVER_RESULT_NO_VALID_CHILD_DEVICE)
		RESULT_CASE_PRINT(DRIVER_RESULT_INVALID_IRQ_NUMBER)
		RESULT_CASE_PRINT(DRIVER_RESULT_OUT_OF_MEMORY)
		RESULT_CASE_PRINT(DRIVER_RESULT_ALREADY_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_NOT_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_NULL_POINTER)
		RESULT_CASE_PRINT(DRIVER_RESULT_UNSUPPORT_DEVICE)
		RESULT_CASE_PRINT(DRIVER_RESULT_UNSUPPORT_FEATURE)
		RESULT_CASE_PRINT(DRIVER_RESULT_INVALID_TYPE)
		RESULT_CASE_PRINT(DRIVER_RESULT_BUSY)
		RESULT_CASE_PRINT(DRIVER_RESULT_EXCEED_MAX_SIZE)
		RESULT_CASE_PRINT(DRIVER_RESULT_OTHER_ERROR)
	}
}

DriverResult register_driver(Driver *driver) {

	driver->state = DRIVER_STATE_UNINITED;
	list_init(&driver->sub_driver_lh);
	list_init(&driver->remapped_memory_lh);
	list_add_tail(&driver->driver_list, &driver_lh);

	Object *object =
		create_object(&driver_object, driver->short_name, driver_object_attr);
	object->value.driver = driver;
	return DRIVER_RESULT_OK;
}

DriverResult unregister_driver(Driver *driver) {
	list_del(&driver->driver_list);
	return DRIVER_RESULT_OK;
}

DriverResult register_sub_driver(
	Driver *driver, SubDriver *sub_driver, DriverType type) {
	sub_driver->driver = driver;
	sub_driver->state  = SUBDRIVER_STATE_UNREADY;
	sub_driver->type   = type;

	wait_queue_init(&sub_driver->wq);
	list_add(&sub_driver->sub_driver_list, &driver->sub_driver_lh);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_sub_driver(Driver *driver, SubDriver *sub_driver) {
	list_del(&sub_driver->sub_driver_list);

	return DRIVER_RESULT_OK;
}

DriverResult driver_init(Driver *driver) {
	DriverResult result;
	if (driver->init != NULL) {
		result = driver->init(driver);
		if (result != DRIVER_RESULT_OK) {
			driver->state = DRIVER_STATE_UNREGISTERED;
			unregister_driver(driver);
			print_error_with_position(
				"driver_init: driver %s init failed!\n",
				driver->short_name.text);
			return result;
		}
	}
	driver->state = DRIVER_STATE_ACTIVE;
	return DRIVER_RESULT_OK;
}

void sub_driver_start_thread(void *arg) {
	SubDriver *sub_driver = arg;
	if (sub_driver->type == DRIVER_TYPE_DEVICE_DRIVER) {
		DeviceDriver *device_driver =
			container_of(sub_driver, DeviceDriver, subdriver);
		Device *device;
		list_for_each_owner (device, &device_driver->device_lh, device_list) {
			if (device->ops->init != NULL) { device->ops->init(device); }
			if (device->ops->start != NULL) { device->ops->start(device); }
		}
		sub_driver->state = SUBDRIVER_STATE_READY;
	} else if (sub_driver->type == DRIVER_TYPE_BUS_DRIVER) {
		BusDriver *bus_driver = container_of(sub_driver, BusDriver, subdriver);
		if (bus_driver->ops->init != NULL) {
			bus_driver->ops->init(bus_driver);
		}

		Bus *bus;
		sub_driver->state = SUBDRIVER_STATE_READY;
		wait_queue_wakeup_all(&bus_driver->subdriver.wq);
		list_for_each_owner (bus, &bus_driver->bus_lh, bus_list) {
			// 先等待Bus Controller Device就绪
			while (bus->controller_device->device_driver->subdriver.state !=
				   SUBDRIVER_STATE_READY) {
				schedule();
			}
			if (bus->ops->scan_bus != NULL) {
				bus->ops->scan_bus(bus_driver, bus);
			}
			if (bus->ops->probe_device != NULL) {
				bus->ops->probe_device(bus_driver, bus);
			}
		}
	}
}

void driver_start_thread(void *arg) {
	Driver *driver = arg;
	check_dependency(driver);
	driver_init(driver);

	struct task_s *cur = get_current_thread();

	SubDriver *sub_driver;
	list_for_each_owner (sub_driver, &driver->sub_driver_lh, sub_driver_list) {
		int old_status = save_and_disable_interrupt();

		char *name;
		if (sub_driver->type == DRIVER_TYPE_DEVICE_DRIVER) {
			DeviceDriver *dd =
				container_of(sub_driver, DeviceDriver, subdriver);
			name = dd->name.text;
		} else if (sub_driver->type == DRIVER_TYPE_BUS_DRIVER) {
			BusDriver *bd = container_of(sub_driver, BusDriver, subdriver);
			name		  = bd->name.text;
		} else name = "sub_driver_start_thread";
		thread_start(
			name, THREAD_DEFAULT_PRIO, sub_driver_start_thread, sub_driver,
			cur);
		store_interrupt_status(old_status);
	}

	thread_wait_children(cur);
}

DriverResult driver_start_all(void) {
	Driver *driver;

	struct task_s *cur = get_current_thread();

	list_for_each_owner (driver, &driver_lh, driver_list) {
		if (driver->state == DRIVER_STATE_UNINITED) {
			int old_status = save_and_disable_interrupt();

			thread_start(
				driver->short_name.text, THREAD_DEFAULT_PRIO,
				driver_start_thread, driver, cur);
			store_interrupt_status(old_status);
		}
	}

	thread_wait_children(cur);
	return DRIVER_RESULT_OK;
}
