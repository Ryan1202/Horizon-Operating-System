/**
 * @file pit.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PIT（Programmable Interval Timer）驱动
 * @date 2022-07-31
 */
#include <driver/interrupt/interrupt_dm.h>
#include <driver/timer/timer_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/fifo.h>
#include <kernel/func.h>
#include <kernel/platform.h>
#include <objects/object.h>
#include <result.h>
#include <stdint.h>
#include <string.h>

#define PIT_MAX_FREQUENCY 1193180

DriverResult pit_init(void *device);
TimerResult	 pit_set_frequency(TimerDevice *timer_device, uint32_t frequency);
DriverResult pit_start(void *device);
DriverResult pit_stop(void *device);
void		 pit_irq_handler(void *device);

extern Driver core_driver;

DeviceOps i8254_device_ops = {
	.init	 = NULL,
	.destroy = NULL,
	.start	 = NULL,
	.stop	 = NULL,
};
DeviceOps pit_device_ops = {
	.init	 = pit_init,
	.destroy = NULL,
	.start	 = pit_start,
	.stop	 = pit_stop,
};
TimerOps pit_timer_ops = {
	.one_shot	   = NULL,
	.set_frequency = pit_set_frequency,
};

DeviceDriver	pit_device_driver;
PhysicalDevice *i8254_device;
TimerDevice	   *pit_timer_device;
DeviceIrq	   *pit_device_irq;

DriverResult register_pit() {
	register_device_driver(&core_driver, &pit_device_driver);

	ObjectAttr attr = device_object_attr;
	DRIVER_RESULT_PASS(
		create_physical_device(&i8254_device, platform_bus, &attr));

	DRIVER_RESULT_PASS(create_timer_device(
		&pit_timer_device, &pit_timer_ops, &pit_device_ops, i8254_device,
		&pit_device_driver));

	register_physical_device(i8254_device, &i8254_device_ops);
	return DRIVER_OK;
}

DriverResult pit_init(void *device) {
	io_out_byte(PIT_CTRL, 0x34);

	pit_timer_device->current_frequency = 0;
	pit_timer_device->min_frequency		= PIT_MAX_FREQUENCY / (uint16_t)-1;
	pit_timer_device->max_frequency		= PIT_MAX_FREQUENCY / 1;
	pit_timer_device->source_frequency	= PIT_MAX_FREQUENCY;
	pit_timer_device->priority			= 1;

	return register_device_irq(
		&pit_device_irq, i8254_device, pit_timer_device->device, PIC_PIT_IRQ,
		pit_irq_handler, IRQ_MODE_EXCLUSIVE);
}

TimerResult pit_set_frequency(TimerDevice *timer_device, uint32_t frequency) {
	uint32_t divisor = PIT_MAX_FREQUENCY / frequency;
	io_out_byte(PIT_CNT0, (uint8_t)(divisor & 0xff));
	io_out_byte(PIT_CNT0, (uint8_t)((divisor >> 8) & 0xff));
	return TIMER_RESULT_OK;
}

DriverResult pit_start(void *device) {
	DRIVER_RESULT_PASS(enable_device_irq(pit_device_irq));
	return DRIVER_OK;
}

DriverResult pit_stop(void *device) {
	DRIVER_RESULT_PASS(disable_device_irq(pit_device_irq));
	return DRIVER_OK;
}

void pit_irq_handler(void *device) {
	timer_irq_handler(device);
}
