#ifndef _INTERRUPT_DM_H
#define _INTERRUPT_DM_H

#include "kernel/device.h"
#include "kernel/device_driver.h"
#include "kernel/device_manager.h"
#include "kernel/driver.h"
#include <stdint.h>

struct InterruptDevice;

typedef struct InterruptDeviceOps {
	int (*redirect_irq)(struct InterruptDevice *device, int irq);
	DriverResult (*enable_irq)(struct InterruptDevice *device, int irq);
	DriverResult (*disable_irq)(struct InterruptDevice *device, int irq);
	void (*eoi)(struct InterruptDevice *device, int irq);
} InterruptDeviceOps;

typedef struct InterruptDevice {
	Device			   *device;
	InterruptDeviceOps *interrupt_ops;
	uint8_t				priority;
} InterruptDevice;

DriverResult register_interrupt_device(
	DeviceDriver *device_driver, Device *device,
	InterruptDevice *interrupt_device);

extern struct DeviceManager interrupt_device_manager;

DriverResult interrupt_dm_start();
uint32_t	 interrupt_redirect_irq(int irq);
DriverResult interrupt_enable_irq(int irq);
DriverResult interrupt_disable_irq(int irq);
void		 interrupt_eoi(int irq);

#endif