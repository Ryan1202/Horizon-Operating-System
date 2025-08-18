#include "kernel/list.h"
#include <driver/timer_dm.h>
#include <kernel/driver.h>
#include <kernel/thread.h>
#include <math.h>
#include <stdint.h>

extern TimerDeviceManager timer_dm_ext;

DriverResult timer_init(Timer *timer) {
	timer->timer_device = timer_dm_ext.scheduler_timer->dm_ext;
	timer->timeout		= 0;
	return DRIVER_RESULT_OK;
}

DriverResult timer_set_timeout(Timer *timer, uint32_t count) {
	if (timer->timer_device == NULL) return DRIVER_RESULT_DEVICE_NOT_EXIST;
	uint32_t counter = timer->timer_device->counter;
	timer->timeout	 = counter + count;
	timer->will_wrap = (timer->timeout > counter) ? false : true;

	return DRIVER_RESULT_OK;
}

DriverResult timer_callback_enable(Timer *timer) {
	if (timer == NULL || timer->timer_device == NULL)
		return DRIVER_RESULT_DEVICE_NOT_EXIST;
	// 在插入时排序
	if (!list_empty(&timer->timer_device->timer_callback_lh)) {
		Timer *last_timer = list_last_owner(
			&timer->timer_device->timer_callback_lh, Timer, list);
		while (last_timer->timeout > timer->timeout) {
			last_timer = list_prev_owner(last_timer, list);
		}
		list_add_after(&timer->list, &last_timer->list);
	} else {
		list_add_tail(&timer->list, &timer->timer_device->timer_callback_lh);
	}
	return DRIVER_RESULT_OK;
}

DriverResult timer_callback_cancel(Timer *timer) {
	if (timer == NULL || timer->timer_device == NULL)
		return DRIVER_RESULT_DEVICE_NOT_EXIST;

	if (list_in_list(&timer->list)) list_del(&timer->list);
	else return DRIVER_RESULT_OTHER_ERROR;

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

	while (!timer_is_timeout(timer))
		;
}

void delay_ms_async(Timer *timer, uint32_t ms) {
	uint32_t count = timer_count_ms(timer, ms);
	timer_set_timeout(timer, count);
	// 设置完立即返回
}

bool timer_is_timeout(Timer *timer) {
	uint32_t counter = timer->timer_device->counter;
	return timer->will_wrap ? timer->timeout >= counter
							: timer->timeout <= counter;
}

size_t timer_get_counter() {
	return ((TimerDevice *)timer_dm_ext.scheduler_timer->dm_ext)->counter;
}
