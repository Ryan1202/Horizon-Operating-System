#include "driver/serial/serial_dm.h"
#include "stdint.h"
#include <bios_emu/bios_emu.h>
#include <driver/interrupt_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/bus/isa/dma.h>
#include <drivers/cmos.h>
#include <drivers/pit.h>
#include <drivers/serial.h>
#include <drivers/vesa_display.h>
#include <drivers/video.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/feature.h>
#include <kernel/func.h>
#include <kernel/list.h>
#include <objects/object.h>
#include <random.h>
#include <string.h>

BusDriverOps platform_ops = {
	.register_bus_hook	 = NULL,
	.unregister_bus_hook = NULL,
};
BusOps platform_bus_ops = {
	.register_device_hook	= NULL,
	.unregister_device_hook = NULL,
};

Driver	  platform_driver;
BusDriver platform_bus_driver = {
	.driver_type	   = DRIVER_TYPE_BUS_DRIVER,
	.bus_type		   = BUS_TYPE_PLATFORM,
	.name			   = STRING_INIT("Platform"),
	.state			   = DRIVER_STATE_UNREGISTERED,
	.private_data_size = 0,
	.ops			   = &platform_ops,
};
Bus platform_bus = {
	.controller_device = NULL,
	.bus_driver		   = &platform_bus_driver,
	.ops			   = &platform_bus_ops,
};

// 完成一些平台必要的准备工作
void platform_early_init() {
	// 初始化段描述符和中断描述符
	init_descriptor();
}

void platform_init() {
	// 因为platform_bus是虚拟的，所以不需要注册device
	ObjectAttr attr = driver_object_attr;
	list_init(&platform_driver.sub_driver_lh);
	register_bus_driver(&platform_driver, &platform_bus_driver, &attr);
	platform_bus.object = platform_bus_driver.object;
	list_init(&platform_bus_driver.bus_lh);
	list_add_tail(&platform_bus.bus_list, &platform_bus_driver.bus_lh);
	list_init(&platform_bus.device_lh);

	read_features();

	bios_emu_init();

	register_serial();
	register_vesa_display();
	register_pic();
	register_apic();
	register_pit();
	register_cmos();

	dma_init();

	if (cpu_check_feature(CPUID_FEAT_TSC)) rand_seed((uint32_t)read_tsc());
}

void serial_receive(uint8_t data) {
	printk("%c", data);
}

void platform_start_devices() {
	Object *serial_object;
	init_and_start(&vesa_display_device);
	init_console();
	interrupt_dm_start(); // 启动由interrupt_dm选择的中断控制器

	open_object_by_path("\\Device\\Serial0", &serial_object);
	serial_device_open(serial_object, SERIAL_BAUD_115200, serial_receive);

	print_features();
	DRV_RESULT_PRINT_CALL(init_and_start, &pit_device);
	DRV_RESULT_PRINT_CALL(init_and_start, &apic_timer_device);
	DRV_RESULT_PRINT_CALL(init_and_start, &rtc_device);

	platform_bus_driver.subdriver.state = SUBDRIVER_STATE_READY;
}