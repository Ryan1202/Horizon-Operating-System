#include "kernel/console.h"
#include <driver/timer/timer_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/thread.h>
#include <result.h>
#include <stdint.h>

const int default_frequencies[] = {100, 250, 1000};

DriverResult timer_dm_load(DeviceManager *manager);
DriverResult timer_dm_unload(DeviceManager *manager);
DriverResult timer_device_init(DeviceManager *manager, LogicalDevice *device);

DeviceManagerOps timer_dm_ops = {
	.dm_load   = timer_dm_load,
	.dm_unload = timer_dm_unload,

	.init_device_hook	 = timer_device_init,
	.start_device_hook	 = NULL,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

TimerDeviceManager timer_dm_ext;

struct DeviceManager timer_dm = {
	.type = DEVICE_TYPE_TIMER,

	.ops = &timer_dm_ops,

	.private_data = &timer_dm_ext,
};

DriverResult timer_dm_load(DeviceManager *manager) {
	timer_dm_ext.scheduler_timer = NULL;

	return DRIVER_OK;
}

DriverResult timer_dm_unload(DeviceManager *manager) {
	TimerDeviceManager *timer_manager = manager->private_data;
	timer_manager->scheduler_timer	  = NULL;

	return DRIVER_OK;
}

DriverResult timer_device_init(DeviceManager *manager, LogicalDevice *device) {
	TimerDevice *timer_device = (TimerDevice *)device->dm_ext;

	const int count =
		sizeof(default_frequencies) / sizeof(typeof(default_frequencies[0]));
	int freq;
	int min_error = 0xfffffff; // 误差
	int i;
	for (i = 0; i < count; i++) {
		int error = timer_device->source_frequency % default_frequencies[i];
		if (error < min_error ||
			(error == min_error && default_frequencies[i] > freq)) {
			min_error = timer_device->source_frequency % default_frequencies[i];
			freq	  = default_frequencies[i];
		}
	}
	DRIVER_RESULT_PASS(timer_set_frequency(device, freq));

	TimerDeviceManager *timer_manager = manager->private_data;
	if (timer_manager->scheduler_timer == NULL) {
		timer_manager->scheduler_timer = device;
	} else {
		TimerDevice *scheduler_timer_device =
			(TimerDevice *)timer_manager->scheduler_timer->dm_ext;
		if (scheduler_timer_device->priority < timer_device->priority) {
			timer_manager->scheduler_timer = device;
		}
	}
	return DRIVER_OK;
}

int timer_get_schedule_tick(int priority) {
	if (priority <= 0) { return 0; }
	TimerDevice *timer_device = (TimerDevice *)timer_dm.private_data;
	int			 ticks = priority * timer_device->current_frequency / 1000;
	if (ticks == 0) { ticks = 1; } // 如果小于粒度，至少1个tick
	return ticks;
}

DriverResult timer_set_frequency(LogicalDevice *device, uint32_t frequency) {
	TimerDevice *timer_device = (TimerDevice *)device->dm_ext;

	TimerResult result =
		timer_device->timer_ops->set_frequency(timer_device, frequency);

	timer_device->current_frequency = frequency;
	timer_device->counter			= 0;
	if (result != TIMER_RESULT_OK) { return DRIVER_ERROR_OTHER; }
	return DRIVER_OK;
}

void timer_irq_handler(LogicalDevice *device) {
	TimerDevice *timer_device = (TimerDevice *)device->dm_ext;
	timer_device->counter++;

	Timer *cur, *next;
	list_for_each_owner_safe (
		cur, next, &timer_device->timer_callback_lh, list) {
		if (!timer_is_timeout(cur)) { break; }

		list_del(&cur->list);
		cur->timeout = 0;
		if (cur->callback != NULL) cur->callback(cur->arg);
	}

	if (!list_empty(&thread_all)) {
		// 已启用多任务
		if (device == timer_dm_ext.scheduler_timer) {
			struct task_s *cur_thread = get_current_thread();
			cur_thread->elapsed_ticks++;

			if (cur_thread->ticks == 0) {
				// printk("need resched\n");
				cur_thread->flags.need_resched = 1;
			} else {
				cur_thread->ticks--;
			}
		}
	}
}

DriverResult create_timer_device(
	TimerDevice **timer_device, TimerOps *timer_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_TIMER);
	if (result != DRIVER_OK) return result;

	*timer_device = kmalloc(sizeof(TimerDevice));
	if (*timer_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	TimerDevice *timer	   = *timer_device;
	logical_device->dm_ext = timer;
	timer->device		   = logical_device;
	timer->timer_ops	   = timer_ops;

	list_init(&timer->timer_callback_lh);

	return DRIVER_OK;
}

DriverResult delete_timer_device(TimerDevice *timer_device) {
	Timer *timer, *next;
	list_for_each_owner_safe (
		timer, next, &timer_device->timer_callback_lh, list) {
		timer->timer_device = NULL;
		timer->timeout		= 0;
		list_del(&timer->list);
	}

	DRIVER_RESULT_PASS(delete_logical_device(timer_device->device));
	int result = kfree(timer_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;

	return DRIVER_OK;
}
