#ifndef _PLATFORM_H

extern struct Bus platform_bus;

void platform_early_init();
void platform_init();
void platform_start_devices();

#endif