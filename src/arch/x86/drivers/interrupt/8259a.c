/**
 * @file 8259a.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PIC驱动
 * @version 1.1
 * @date 2022-07-31
 *
 */
#include <bits.h>
#include <driver/interrupt/interrupt_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/platform.h>
#include <objects/object.h>
#include <result.h>
#include <string.h>

DriverResult i8259a_init(void *device);
int			 pic_redirect_irq(InterruptDevice *device, int irq);
DriverResult pic_enable_irq(InterruptDevice *device, int irq);
DriverResult pic_disable_irq(InterruptDevice *device, int irq);
void		 pic_eoi(InterruptDevice *device, int irq);

extern Driver core_driver;

DeviceOps i8259a_device_ops = {
	.init	 = i8259a_init,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};
DeviceOps pic_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};
InterruptDeviceOps pic_interrupt_ops = {
	.enable_irq	  = pic_enable_irq,
	.disable_irq  = pic_disable_irq,
	.eoi		  = pic_eoi,
	.redirect_irq = pic_redirect_irq,
};

DeviceDriver	 pic_device_driver;
PhysicalDevice	*i8259a_device;
InterruptDevice *pic_device;
InterruptDevice	 pic_interrupt_device = {
	 .ops	   = &pic_interrupt_ops,
	 .priority = 0,
};

DriverResult register_pic(void) {
	ObjectAttr attr = device_object_attr;

	if (use_apic) return DRIVER_OK;

	DRIVER_RESULT_PASS(
		register_device_driver(&core_driver, &pic_device_driver));

	DRIVER_RESULT_PASS(
		create_physical_device(&i8259a_device, platform_bus, &attr));

	DRIVER_RESULT_PASS(create_interrupt_device(
		&pic_device, &pic_device_ops, i8259a_device, &pic_device_driver,
		&pic_interrupt_ops, 0));

	register_physical_device(i8259a_device, &i8259a_device_ops);
	return DRIVER_OK;
}

void mask_8259a(void) {
	io_out_byte(PIC0_IMR, 0xff);
	io_out_byte(PIC1_IMR, 0xff);
}

DriverResult i8259a_init(void *device) {
	io_out_byte(PIC0_IMR, 0xff); // 屏蔽主PIC的所有中断
	io_out_byte(PIC1_IMR, 0xff); // 屏蔽从PIC的所有中断

	io_out_byte(PIC0_ICW1, 0x11); // 级联，边沿触发
	io_out_byte(PIC0_ICW2, 0x20); // 起始中断向量号位0x20(0x00~0x1f为内部中断)
	io_out_byte(PIC0_ICW3, 1 << 2); // IRQ2用于连接从PIC
	// 全嵌套模式，非缓冲模式，手动结束中断，x86处理器
	io_out_byte(PIC0_ICW4, 0x01);

	io_out_byte(PIC1_ICW1, 0x11); // 级联，边沿触发
	io_out_byte(PIC1_ICW2, 0x28); // 起始中断向量号位0x28
	io_out_byte(PIC1_ICW3, 0x02); // 连接到主PIC的IRQ2
	// 全嵌套模式，非缓冲模式，手动结束中断，x86处理器
	io_out_byte(PIC1_ICW4, 0x01);

	io_out_byte(PIC0_IMR, 0xfb); // 开启IRQ2(从PIC)中断
	io_out_byte(PIC1_IMR, 0xff); // 屏蔽从PIC的所有中断
	return DRIVER_OK;
}

int pic_redirect_irq(InterruptDevice *device, int irq) {
	return irq;
}

DriverResult pic_enable_irq(InterruptDevice *device, int irq) {
	uint8_t data;
	if (irq < 8) {
		data = io_in8(PIC0_IMR);
		io_out8(PIC0_IMR, BIN_DIS(data, BIT(irq)));
	} else {
		data = io_in8(PIC1_IMR);
		io_out8(PIC1_IMR, BIN_DIS(data, BIT(irq % 8)));
	}
	return DRIVER_OK;
}

DriverResult pic_disable_irq(InterruptDevice *device, int irq) {
	uint8_t data;
	if (irq < 8) {
		data = io_in8(PIC0_IMR);
		io_out8(PIC0_IMR, BIN_EN(data, BIT(irq)));
	} else {
		data = io_in8(PIC1_IMR);
		io_out8(PIC1_IMR, BIN_EN(data, BIT(irq % 8)));
	}
	return DRIVER_OK;
}

void pic_eoi(InterruptDevice *device, int irq) {
	if (irq >= 8) { io_out8(PIC1_OCW1, PIC_EOI); }
	io_out8(PIC0_OCW1, PIC_EOI);
}
