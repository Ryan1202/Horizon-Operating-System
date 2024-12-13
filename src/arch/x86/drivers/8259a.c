/**
 * @file 8259a.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PIC驱动
 * @version 1.1
 * @date 2022-07-31
 *
 */
#include "string.h"
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <kernel/descriptor.h>
#include <kernel/func.h>

// --------new--------
#include <bits.h>
#include <driver/interrupt_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/platform.h>
#include <result.h>

DriverResult pic_init(Device *device);
int			 pic_redirect_irq(InterruptDevice *device, int irq);
DriverResult pic_enable_irq(InterruptDevice *device, int irq);
DriverResult pic_disable_irq(InterruptDevice *device, int irq);
void		 pic_eoi(InterruptDevice *device, int irq);

DeviceDriverOps pic_device_driver_ops = {
	.register_driver_hook	= NULL,
	.unregister_driver_hook = NULL,
};
DeviceOps pic_device_ops = {
	.init	 = pic_init,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};
InterruptDeviceOps pic_interrupt_ops = {
	.enable_irq	  = pic_enable_irq,
	.disable_irq  = pic_disable_irq,
	.eoi		  = pic_eoi,
	.redirect_irq = pic_redirect_irq,
};

Driver		 pic_driver		   = {.name = STRING_INIT("PIC")};
DeviceDriver pic_device_driver = {
	.name	  = STRING_INIT("PIC Driver"),
	.type	  = DEVICE_TYPE_INTERRUPT_CONTROLLER,
	.bus	  = &platform_bus,
	.priority = DRIVER_PRIORITY_BASIC,
	.state	  = DRIVER_STATE_UNREGISTERED,
	.ops	  = &pic_device_driver_ops,
};
Device pic_device = {
	.name = STRING_INIT("8259a"),

	.device_driver = &pic_device_driver,

	.ops = &pic_device_ops,

	.private_data_size = 0,
};
InterruptDevice pic_interrupt_device = {
	.interrupt_ops = &pic_interrupt_ops,
	.priority	   = 0,
};

void register_pic(void) {
	register_driver(&pic_driver);
	driver_init(&pic_driver);
	register_device_driver(&pic_driver, &pic_device_driver);
	register_interrupt_device(
		&pic_device_driver, &pic_device, &pic_interrupt_device);
}

void mask_8259a(void) {
	io_out_byte(PIC0_IMR, 0xff);
	io_out_byte(PIC1_IMR, 0xff);
}

DriverResult pic_init(Device *device) {
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
	return DRIVER_RESULT_OK;
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
	return DRIVER_RESULT_OK;
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
	return DRIVER_RESULT_OK;
}

void pic_eoi(InterruptDevice *device, int irq) {
	if (irq >= 8) { io_out8(PIC1_OCW1, PIC_EOI); }
	io_out8(PIC0_OCW1, PIC_EOI);
}

// --------old--------

// void init_8259a(void) {
// 	io_out8(PIC0_IMR, 0xff); // 屏蔽主PIC的所有中断
// 	io_out8(PIC1_IMR, 0xff); // 屏蔽从PIC的所有中断

// 	io_out8(PIC0_ICW1, 0x11); // 级联，边沿触发
// 	io_out8(PIC0_ICW2, 0x20); // 起始中断向量号位0x20(0x00~0x1f为内部中断)
// 	io_out8(PIC0_ICW3, 1 << 2); // IRQ2用于连接从PIC
// 	io_out8(PIC0_ICW4, 0x01); // 全嵌套模式，非缓冲模式，手动结束中断，x86处理器

// 	io_out8(PIC1_ICW1, 0x11); // 级联，边沿触发
// 	io_out8(PIC1_ICW2, 0x28); // 起始中断向量号位0x28
// 	io_out8(PIC1_ICW3, 0x02); // 连接到主PIC的IRQ2
// 	io_out8(PIC1_ICW4, 0x01); // 全嵌套模式，非缓冲模式，手动结束中断，x86处理器

// 	io_out8(PIC0_IMR, 0xfb); // 开启IRQ2(从PIC)中断
// 	io_out8(PIC1_IMR, 0xff); // 屏蔽从PIC的所有中断

// 	use_apic = 0;

// 	return;
// }

// void mask_8259a(void) {
// 	io_out8(PIC0_IMR, 0xff);
// 	io_out8(PIC1_IMR, 0xff);
// }

// void pic_enable_irq(int irq) {
// 	uint8_t data;
// 	if (irq < 8) {
// 		data = io_in8(PIC0_IMR);
// 		io_out8(PIC0_IMR, data & ~(1 << irq));
// 	} else {
// 		data = io_in8(PIC1_IMR);
// 		io_out8(PIC1_IMR, data & ~(1 << (irq % 8)));
// 	}
// }

// void pic_eoi(int irq) {
// 	if (irq >= 8) { io_out8(PIC1_OCW1, PIC_EOI); }
// 	io_out8(PIC0_OCW1, PIC_EOI);
// }
