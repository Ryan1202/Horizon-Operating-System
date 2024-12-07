#include <driver/interrupt_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/pit.h>
#include <drivers/vesa_display.h>
#include <drivers/video.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/feature.h>

BusDriverOps platform_ops = {
	.register_device_hook	= NULL,
	.unregister_device_hook = NULL,
};

Driver	  platform_driver;
BusDriver platform_bus = {
	.driver_type	   = DRIVER_TYPE_BUS_DRIVER,
	.bus_type		   = BUS_TYPE_PLATFORM,
	.name			   = STRING_INIT("platform"),
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
};

void init_platform() {
	init_descriptor();
	init_memory();

	register_bus_driver(&platform_driver, &platform_bus);
	read_features();

	register_vesa_display();
	register_pic();
	register_apic();
	register_pit();
}

void platform_init_and_start_devices() {
	init_and_start(&vesa_display_device);
	init_console();
	print_features();
	interrupt_dm_start(); // 启动由interrupt_dm选择的中断控制器
	DRV_RESULT_PRINT_CALL(init_and_start, &pit_device);
	DRV_RESULT_PRINT_CALL(init_and_start, &apic_timer_device);
}