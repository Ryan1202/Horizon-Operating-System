#ifndef _TIMER_DM_H
#define _TIMER_DM_H

#include "kernel/device.h"
#include "kernel/device_driver.h"
#include "result.h"
#include "stdint.h"

typedef enum TimerResult {
	TIMER_RESULT_OK = 0,
	TIMER_RESULT_FREQ_TOO_LARGE,
	TIMER_RESULT_FREQ_TOO_SMALL,
	TIMER_RESULT_OTHER_ERROR,
} TimerResult;

#define TIMER_RESULT_DELIVER_CALL(func, ...) \
	RESULT_DELIVER_CALL(TimerResult, TIMER_RESULT_OK, func, {}, __VA_ARGS__)

struct TimerDevice;

typedef struct TimerOps {
	TimerResult (*set_frequency)(
		struct TimerDevice *timer_device, uint32_t frequency);
	TimerResult (*one_shot)(
		struct TimerDevice *timer_device,
		uint32_t frequency); // 单次中断，可用于校准其他定时器
} TimerOps;

typedef struct TimerDevice {
	list_t timer_list_lh;

	Device	*device;
	uint32_t current_frequency;
	uint32_t min_frequency;
	uint32_t max_frequency;
	uint32_t source_frequency;
	uint32_t priority;

	uint32_t counter;

	TimerOps *timer_ops;
} TimerDevice;

typedef struct Timer {
	list_t list;

	TimerDevice *timer_device;
	uint32_t	 timeout;
} Timer;

extern struct DeviceManager timer_device_manager;

DriverResult register_timer_device(
	DeviceDriver *device_driver, Device *device, TimerDevice *timer_device);
DriverResult timer_init(Timer *timer);
void		 timer_irq_handler(Device *device);
int			 timer_get_schedule_tick(int priority);
DriverResult timer_set_frequency(Device *device, uint32_t frequency);
void		 delay_ms(Timer *timer, uint32_t ms);
void		 delay_ms_async(Timer *timer, uint32_t ms);
bool		 timer_is_timeout(Timer *timer);

#endif