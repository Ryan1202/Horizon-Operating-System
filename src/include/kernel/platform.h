#ifndef _PLATFORM_H

#include <kernel/driver.h>
#include <types.h>

extern struct Bus			 *platform_bus;
extern struct PhysicalDevice *platform_device;

extern unsigned char VIR_BASE[];

#define get_vaddr_base() (size_t)(VIR_BASE)

void		 platform_early_init();
DriverResult platform_init();
void		 platform_start_devices();

#endif