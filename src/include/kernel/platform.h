#ifndef _PLATFORM_H

#include <kernel/driver.h>

extern struct Bus			 *platform_bus;
extern struct PhysicalDevice *platform_device;

void		 platform_early_init();
DriverResult platform_init();
void		 platform_start_devices();

#endif