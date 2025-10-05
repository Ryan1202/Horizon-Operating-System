#ifndef _PIT_H
#define _PIT_H

#include "kernel/driver.h"
#define PIT_CTRL 0x43
#define PIT_CNT0 0x40

#define PIC_PIT_IRQ	 0 // 使用PIC时PIT的IRQ为1
#define APIC_PIT_IRQ 2 // 使用APIC时PIT的IRQ为2

#define MAX_TIMER 2048

#define TIMER_FREE	 0
#define TIMER_UNUSED 1
#define TIMER_USING	 2

DriverResult				  register_pit();
extern struct PhysicalDevice *i8254_device;
extern struct TimerDevice	 *pit_timer_device;

#endif