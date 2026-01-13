#ifndef _TIMER_DM_H
#define _TIMER_DM_H

#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/list.h>
#include <result.h>
#include <stdint.h>

typedef enum TimerResult {
	TIMER_RESULT_OK = 0,
	TIMER_RESULT_FREQ_TOO_LARGE,
	TIMER_RESULT_FREQ_TOO_SMALL,
	TIMER_RESULT_UNSUPPORT,
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
	list_t timer_callback_lh;

	LogicalDevice *device;
	uint32_t	   current_frequency;
	uint32_t	   min_frequency;
	uint32_t	   max_frequency;
	uint32_t	   source_frequency;
	uint32_t	   priority;

	uint32_t counter;

	TimerOps *timer_ops;
} TimerDevice;

typedef void (*TimerCallback)(void *arg);

typedef struct Timer {
	list_t list;

	TimerDevice *timer_device;

	bool	 will_wrap; // 计时器是否会溢出导致从0重新开始计数
	uint32_t timeout;	// 计时器超时时间

	TimerCallback callback;
	void		 *arg;
} Timer;

typedef struct TimerDeviceManager {
	LogicalDevice *scheduler_timer;
} TimerDeviceManager;

extern struct DeviceManager timer_dm;

DriverResult create_timer_device(
	TimerDevice **timer_device, TimerOps *timer_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver);
DriverResult delete_timer_device(TimerDevice *timer_device);

DriverResult timer_init(Timer *timer);
void		 timer_irq_handler(LogicalDevice *device);
uint32_t	 timer_count_ms(Timer *timer, uint32_t ms);
int			 timer_get_schedule_tick(int priority);
DriverResult timer_set_frequency(LogicalDevice *device, uint32_t frequency);
void		 delay_ms(Timer *timer, uint32_t ms);
void		 delay_ms_async(Timer *timer, uint32_t ms);
bool		 timer_is_timeout(Timer *timer);
DriverResult timer_set_timeout(Timer *timer, uint32_t count);
size_t		 timer_get_counter();

DriverResult timer_callback_enable(Timer *timer);
DriverResult timer_callback_cancel(Timer *timer);

#endif