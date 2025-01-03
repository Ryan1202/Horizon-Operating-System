#include <driver/timer_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <result.h>
#include <stdint.h>

const int default_frequencies[] = {100, 250, 1000};

DriverResult timer_dm_load(DeviceManager *manager);
DriverResult timer_dm_unload(DeviceManager *manager);
DriverResult timer_device_init(DeviceManager *manager, Device *device);

DeviceManagerOps timer_dm_ops = {
	.dm_load_hook	= timer_dm_load,
	.dm_unload_hook = timer_dm_unload,

	.init_device_hook	 = timer_device_init,
	.start_device_hook	 = NULL,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

TimerDeviceManager timer_dm_ext;

struct DeviceManager timer_device_manager = {
	.type = DEVICE_TYPE_TIMER,

	.ops = &timer_dm_ops,

	.private_data = &timer_dm_ext,
};

DriverResult timer_dm_load(DeviceManager *manager) {
	TimerDeviceManager *timer_manager = manager->private_data;
	timer_manager->scheduler_timer	  = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult timer_dm_unload(DeviceManager *manager) {
	TimerDeviceManager *timer_manager = manager->private_data;
	timer_manager->scheduler_timer	  = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult timer_device_init(DeviceManager *manager, Device *device) {
	TimerDevice *timer_device = (TimerDevice *)device->device_manager_extension;

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
	DRV_RESULT_DELIVER_CALL(timer_set_frequency, device, freq);

	TimerDeviceManager *timer_manager = manager->private_data;
	if (timer_manager->scheduler_timer == NULL) {
		timer_manager->scheduler_timer = device;
	} else {
		TimerDevice *scheduler_timer_device =
			(TimerDevice *)
				timer_manager->scheduler_timer->device_manager_extension;
		if (scheduler_timer_device->priority < timer_device->priority) {
			timer_manager->scheduler_timer = device;
		}
	}
	return DRIVER_RESULT_OK;
}

int timer_get_schedule_tick(int priority) {
	if (priority <= 0) { return 0; }
	TimerDevice *timer_device =
		(TimerDevice *)timer_device_manager.private_data;
	int ticks = priority * timer_device->current_frequency / 1000;
	if (ticks == 0) { ticks = 1; } // 如果小于粒度，至少1个tick
	return ticks;
}

DriverResult timer_set_frequency(Device *device, uint32_t frequency) {
	TimerDevice *timer_device = (TimerDevice *)device->device_manager_extension;

	TimerResult result =
		timer_device->timer_ops->set_frequency(timer_device, frequency);

	timer_device->current_frequency = frequency;
	timer_device->counter			= 0;
	if (result != TIMER_RESULT_OK) { return DRIVER_RESULT_OTHER_ERROR; }
	return DRIVER_RESULT_OK;
}

void timer_irq_handler(Device *device) {
	TimerDevice *timer_device = (TimerDevice *)device->device_manager_extension;
	timer_device->counter++;

	Timer *cur, *next;
	list_for_each_owner_safe (cur, next, &timer_device->timer_list_lh, list) {
		if (cur->timeout > timer_device->counter) { break; }
		list_del(&cur->list);
		if (cur->period) {
			timer_set_timeout(cur, cur->period);
		} else {
			cur->timeout = 0;
		}
	}

	if (!list_empty(&thread_all)) {
		// 已启用多任务
		if (device == timer_dm_ext.scheduler_timer) {
			struct task_s *cur_thread = get_current_thread();
			cur_thread->elapsed_ticks++;

			if (cur_thread->ticks == 0) {
				schedule();
			} else {
				cur_thread->ticks--;
			}
		}
	}
}

DriverResult register_timer_device(
	DeviceDriver *device_driver, Device *device, TimerDevice *timer_device) {

	device->device_manager_extension = timer_device;
	timer_device->device			 = device;
	DRV_RESULT_DELIVER_CALL(
		register_device, device_driver, device_driver->bus, device);
	list_init(&timer_device->timer_list_lh);
	list_add_tail(&device->dm_list, &timer_device_manager.device_lh);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_timer_device(
	DeviceDriver *device_driver, Device *device, TimerDevice *timer_device) {
	Timer *timer, *next;
	list_for_each_owner_safe (timer, next, &timer_device->timer_list_lh, list) {
		timer->timer_device = NULL;
		timer->timeout		= 0;
		list_del(&timer->list);
	}

	DRV_RESULT_DELIVER_CALL(unregister_device, device_driver, device);
	list_del(&device->device_list);
	return DRIVER_RESULT_OK;
}
