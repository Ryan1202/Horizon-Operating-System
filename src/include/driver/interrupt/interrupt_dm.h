#ifndef _INTERRUPT_DM_H
#define _INTERRUPT_DM_H

#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <stdint.h>

struct InterruptDevice;

typedef struct InterruptDeviceOps {
	int (*redirect_irq)(
		struct InterruptDevice *device, int irq, struct IrqDomain *domain);
	DriverResult (*enable_irq)(struct InterruptDevice *device, int irq);
	DriverResult (*disable_irq)(struct InterruptDevice *device, int irq);
	void (*eoi)(struct InterruptDevice *device, int irq);
} InterruptDeviceOps;

typedef struct IrqDomain {
} IrqDomain;

typedef struct InterruptDevice {
	LogicalDevice	   *device;
	InterruptDeviceOps *ops;
	uint8_t				priority;
} InterruptDevice;

DriverResult create_interrupt_device(
	InterruptDevice **interrupt_device, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver,
	InterruptDeviceOps *int_ops, int priority);

extern struct DeviceManager interrupt_dm;

DriverResult interrupt_dm_start();
uint32_t	 interrupt_redirect_irq(int irq, IrqDomain *domain);
DriverResult interrupt_enable_irq(int irq);
DriverResult interrupt_disable_irq(int irq);
void		 interrupt_eoi(int irq);

#endif