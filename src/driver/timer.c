#include <driver/timer_dm.h>
#include <kernel/driver.h>
#include <kernel/thread.h>
#include <math.h>

extern TimerDeviceManager timer_dm_ext;

DriverResult timer_init(Timer *timer) {
	timer->timer_device =
		timer_dm_ext.scheduler_timer->device_manager_extension;
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
	timer->period  = 0;
	timer_set_timeout(timer, count);

	while (timer->timeout != 0)
		;
}

void delay_ms_async(Timer *timer, uint32_t ms) {
	uint32_t count = timer_count_ms(timer, ms);
	timer->period  = 0;
	timer_set_timeout(timer, count);
	// 设置完立即返回
}

void set_periodic_ms(Timer *timer, uint32_t ms) {
	uint32_t count = timer_count_ms(timer, ms);
	timer->period  = count;
	timer_set_timeout(timer, count);
}

bool timer_is_timeout(Timer *timer) {
	return timer->timeout == 0;
}
