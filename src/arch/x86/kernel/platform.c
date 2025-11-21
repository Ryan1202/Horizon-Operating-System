#include <bios_emu/bios_emu.h>
#include <driver/framebuffer/fb_dm.h>
#include <driver/interrupt/interrupt_dm.h>
#include <driver/serial/serial_dm.h>
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
#include <stdint.h>
#include <string.h>

BusOps platform_bus_ops = {
	.scan_bus	  = NULL,
	.probe_device = NULL,
};
DeviceOps platform_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};

Driver	  platform_driver;
BusDriver platform_bus_driver = {
	.name = STRING_INIT("Platform"),
};
Bus *platform_bus;

DeviceDriver	platform_device_driver;
PhysicalDevice *platform_device;

// 完成一些平台必要的准备工作
void platform_early_init() {
	// 初始化段描述符和中断描述符
	init_descriptor();

	// 读取CPU特性
	read_features();
}

DriverResult platform_init() {
	DriverResult result;

	// 因为platform_bus是虚拟的，所以不需要注册device
	ObjectAttr attr = driver_object_attr;
	register_driver(&platform_driver);
	register_bus_driver(
		&platform_driver, BUS_TYPE_PLATFORM, &platform_bus_driver, &attr);
	register_device_driver(&platform_driver, &platform_device_driver);

	result = create_bus(&platform_bus, &platform_bus_driver, &platform_bus_ops);
	if (result != DRIVER_OK) { return result; }

	result = create_physical_device(&platform_device, platform_bus, &attr);
	if (result != DRIVER_OK) { return result; }
	register_physical_device(platform_device, &platform_device_ops);

	bios_emu_init();

	register_serial();
	result = register_vesa_display();
	// result = register_apic();
	result = register_pic();
	result = register_pit();
	result = register_cmos();

	// dma_init();

	if (cpu_check_feature(CPUID_FEAT_TSC)) rand_seed((uint32_t)read_tsc());

	return DRIVER_OK;
}

void serial_receive(uint8_t data) {
	printk("%c", data);
}

void platform_start_devices() {
	init_and_start_physical_device(i8254_device);
	init_and_start_physical_device(cmos_device);

	Object *serial_object;
	framebuffer_start_all();
	init_console();
	interrupt_dm_start(); // 启动由interrupt_dm选择的中断控制器

	ObjectResult result;
	result = open_object_by_path("\\Device\\Serial0", &serial_object);
	if (result == OBJECT_OK) {
		serial_device_open(serial_object, SERIAL_BAUD_115200, serial_receive);
	}

	// print_features();
	DRV_RESULT_PRINT_CALL(
		init_and_start_logical_device(pit_timer_device->device));
	// if (use_apic)
	// 	DRV_RESULT_PRINT_CALL(
	// 		init_and_start_logical_device(apic_timer_device->device));
	DRV_RESULT_PRINT_CALL(
		init_and_start_logical_device(rtc_time_device->device));

	// platform_bus_driver.subdriver.state = SUBDRIVER_STATE_READY;
}