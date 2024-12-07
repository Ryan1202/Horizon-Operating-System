#include <driver/timer_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <math.h>
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

typedef struct TimerDeviceManager {
	Device *scheduler_timer;
} TimerDeviceManager;

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
	TimerDevice *timer_device = (TimerDevice *)device->driver_manager_extension;

	const int count =
		sizeof(default_frequencies) / sizeof(typeof(default_frequencies[0]));
	int freq;
	int error = 0xfffffff;
	int i;
	for (i = 0; i < count; i++) {
		if (timer_device->source_frequency % default_frequencies[i] < error) {
			error = timer_device->source_frequency % default_frequencies[i];
			freq  = default_frequencies[i];
		}
	}
	DRV_RESULT_DELIVER_CALL(timer_set_frequency, device, freq);

	TimerDeviceManager *timer_manager = manager->private_data;
	if (timer_manager->scheduler_timer == NULL) {
		timer_manager->scheduler_timer = device;
	} else {
		TimerDevice *scheduler_timer_device =
			(TimerDevice *)
				timer_manager->scheduler_timer->driver_manager_extension;
		if (scheduler_timer_device->priority < timer_device->priority) {
			timer_manager->scheduler_timer = device;
		}
	}
	return DRIVER_RESULT_OK;
}

DriverResult timer_set_frequency(Device *device, uint32_t frequency) {
	TimerDevice *timer_device = (TimerDevice *)device->driver_manager_extension;

	TimerResult result =
		timer_device->timer_ops->set_frequency(timer_device, frequency);

	timer_device->current_frequency = frequency;
	timer_device->counter			= 0;
	if (result != TIMER_RESULT_OK) { return DRIVER_RESULT_OTHER_ERROR; }
	return DRIVER_RESULT_OK;
}

void timer_irq_handler(Device *device) {
	TimerDevice *timer_device = (TimerDevice *)device->driver_manager_extension;
	timer_device->counter++;

	Timer *cur, *next;
	list_for_each_owner_safe (cur, next, &timer_device->timer_list_lh, list) {
		if (cur->timeout > timer_device->counter) { break; }
		list_del(&cur->list);
		cur->timeout = 0;
	}
}

DriverResult register_timer_device(
	DeviceDriver *device_driver, Device *device, TimerDevice *timer_device) {

	device->driver_manager_extension = timer_device;
	timer_device->device			 = device;
	DRV_RESULT_DELIVER_CALL(register_device, device_driver, device);
	list_init(&timer_device->timer_list_lh);
	list_add_tail(&device->dm_list, &timer_device_manager.device_driver_lh);

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

DriverResult timer_init(Timer *timer) {
	timer->timer_device =
		timer_dm_ext.scheduler_timer->driver_manager_extension;
	timer->timeout = 0;
	return DRIVER_RESULT_OK;
}

DriverResult timer_set_timeout(Timer *timer, uint32_t count) {
	if (timer->timer_device == NULL) return DRIVER_RESULT_DEVICE_NOT_EXIST;
	timer->timeout = timer->timer_device->counter + count;

	// 在插入时排序
	if (!list_empty(&timer->timer_device->timer_list_lh)) {
		Timer *last_timer =
			list_last_owner(&timer->timer_device->timer_list_lh, Timer, list);
		while (last_timer->timeout > timer->timeout) {
			last_timer = list_prev_onwer(last_timer, list);
		}
		list_add_after(&timer->list, &last_timer->list);
	} else {
		list_add_tail(&timer->list, &timer->timer_device->timer_list_lh);
	}
	return DRIVER_RESULT_OK;
}

uint32_t timer_count_ms(Timer *timer, uint32_t ms) {
	uint32_t freq = timer->timer_device->current_frequency;
	if (freq < 1000) {
		return DIV_ROUND_UP(ms * freq, 1000);
	} else {
		return DIV_ROUND_UP(ms, (freq / 1000));
	}
}

void delay_ms(Timer *timer, uint32_t ms) {
	uint32_t count = timer_count_ms(timer, ms);
	timer_set_timeout(timer, count);

	while (timer->timeout != 0)
		;
}

void delay_ms_async(Timer *timer, uint32_t ms) {
	uint32_t count = timer_count_ms(timer, ms);
	timer_set_timeout(timer, count);
	// 设置完立即返回
}

bool timer_is_timeout(Timer *timer) {
	return timer->timeout == 0;
}
