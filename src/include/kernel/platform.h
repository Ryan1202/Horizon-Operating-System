#include <stdint.h>
#ifndef _PLATFORM_H

#include <kernel/driver.h>
#include <types.h>

extern struct Bus			 *platform_bus;
extern struct PhysicalDevice *platform_device;

extern unsigned char VIR_BASE[];

#define get_vaddr_base() (size_t)(VIR_BASE)

typedef struct {
	uint8_t	 use_i8042;
	uint8_t	 use_vga;
	uint8_t	 use_msi;
	uint8_t	 use_pcie_aspm;
	uint8_t	 use_rtc;
	uint8_t	 has_pic;
	uint32_t lapic_address;
} BootCapabilities;

extern BootCapabilities x86_boot_capabilities;

void		 platform_early_init();
DriverResult platform_init();
void		 platform_start_devices();
void		 acpi_update_boot_capabilities();

#endif