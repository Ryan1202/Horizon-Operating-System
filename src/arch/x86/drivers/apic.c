/**
 * @file apic.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief APIC驱动(参考xv6)
 * @version 0.1 Alpha
 * @date 2021-06
 */
#include <bits.h>
#include <driver/timer_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/cmos.h>
#include <drivers/msr.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/driver_interface.h>
#include <kernel/feature.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/page.h>
#include <kernel/thread.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

void apic_timer_handler(void);

char use_apic;

volatile struct ioapic {
	uint32_t reg;
	uint32_t pad[3];
	uint32_t data;
};

// --------new---------
#include <driver/interrupt_dm.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/platform.h>

DriverResult apic_init(Device *device);
DriverResult apic_start(Device *device);
DriverResult apic_stop(Device *device);
DriverResult apic_driver_init(struct DeviceDriver *driver);
int			 apic_redirect_irq(InterruptDevice *device, int irq);
DriverResult apic_enable_irq(InterruptDevice *device, int irq);
DriverResult apic_disable_irq(InterruptDevice *device, int irq);
DriverResult apic_timer_init(Device *device);
DriverResult apic_timer_start(Device *device);
DriverResult apic_timer_stop(Device *device);
void		 apic_eoi(InterruptDevice *device, int irq);
TimerResult	 apic_timer_set_frequency(
	 TimerDevice *timer_device, uint32_t frequency);
void apic_timer_irq_handler(Device *device);

typedef struct ApicInfo {
	enum {
		APIC_TYPE_XAPIC,
		APIC_TYPE_X2APIC,
	} apic_type;
	uint32_t	   apic_base;
	uint32_t	   apic_base_high; // 仅在使用x2APIC有用
	uint32_t	   apic_id;
	uint32_t	   apic_id_high; // 仅在使用x2APIC有用
	uint8_t		   version;
	uint8_t		   max_lvt_entry;
	uint32_t	  *lapic_mmio;
	struct ioapic *ioapic;
} ApicInfo;
ApicInfo apic_info;

uint32_t lapic_write(int index, int value) {
	apic_info.lapic_mmio[index / 4] = value;
	return apic_info.lapic_mmio[APIC_ID / 4];
}

uint32_t lapic_read(int index) {
	return apic_info.lapic_mmio[index / 4];
}

DeviceDriverOps apic_device_driver_ops = {
	.register_driver_hook	= NULL,
	.unregister_driver_hook = NULL,
};
DeviceOps apic_device_ops = {
	.init	 = apic_init,
	.start	 = apic_start,
	.stop	 = apic_stop,
	.destroy = NULL,
	.status	 = NULL,
};
InterruptDeviceOps apic_interrupt_ops = {
	.enable_irq	  = apic_enable_irq,
	.disable_irq  = apic_disable_irq,
	.eoi		  = apic_eoi,
	.redirect_irq = apic_redirect_irq,
};

DeviceDriverOps apic_timer_device_driver_ops = {
	.register_driver_hook	= NULL,
	.unregister_driver_hook = NULL,
};
DeviceOps apic_timer_device_ops = {
	.init	 = apic_timer_init,
	.start	 = apic_timer_start,
	.stop	 = apic_timer_stop,
	.destroy = NULL,
	.status	 = NULL,
};
TimerOps apic_timer_ops = {
	.set_frequency = apic_timer_set_frequency,
};

Driver apic_driver = {.name = STRING_INIT("APIC")};

DeviceDriver apic_device_driver = {
	.name	  = STRING_INIT("APIC"),
	.bus	  = &platform_bus,
	.type	  = DEVICE_TYPE_INTERRUPT_CONTROLLER,
	.priority = DRIVER_PRIORITY_BASIC,
	.state	  = DRIVER_STATE_UNREGISTERED,
	.ops	  = &apic_device_driver_ops,
};
Device apic_device = {
	.name			   = STRING_INIT("APIC"),
	.device_driver	   = &apic_device_driver,
	.ops			   = &apic_device_ops,
	.private_data_size = 0,
};
InterruptDevice apic_interrupt_device = {
	.interrupt_ops = &apic_interrupt_ops,
	.priority	   = 1,
};

DeviceDriver apic_timer_device_driver = {
	.name	  = STRING_INIT("APIC Timer"),
	.bus	  = &platform_bus,
	.type	  = DEVICE_TYPE_TIMER,
	.priority = DRIVER_PRIORITY_BASIC,
	.state	  = DRIVER_STATE_UNREGISTERED,
	.ops	  = &apic_timer_device_driver_ops,
};
DeviceIrq apic_timer_irq = {
	.device	 = &apic_timer_device,
	.irq	 = PIC_PIT_IRQ,
	.handler = apic_timer_irq_handler,
};
Device apic_timer_device = {
	.name			   = STRING_INIT("APIC Timer"),
	.device_driver	   = &apic_timer_device_driver,
	.ops			   = &apic_timer_device_ops,
	.irq			   = &apic_timer_irq,
	.private_data_size = 0,
};
TimerDevice apic_timer_timer_device = {
	.current_frequency = 0,
	.min_frequency	   = 0,
	.max_frequency	   = 0,
	.source_frequency  = 0,
	.priority		   = 2,
	.timer_ops		   = &apic_timer_ops,
};

uint32_t io_apic_read(uint32_t reg) {
	apic_info.ioapic->reg = reg;
	return apic_info.ioapic->data;
}

void io_apic_write(uint32_t reg, uint32_t data) {
	apic_info.ioapic->reg  = reg;
	apic_info.ioapic->data = data;
}

DriverResult register_apic(void) {
	register_driver(&apic_driver);
	register_device_driver(&apic_driver, &apic_device_driver);
	register_device_driver(&apic_driver, &apic_timer_device_driver);
	register_interrupt_device(
		&apic_device_driver, &apic_device, &apic_interrupt_device);
	register_timer_device(
		&apic_device_driver, &apic_timer_device, &apic_timer_timer_device);
	return DRIVER_RESULT_OK;
}

DriverResult apic_driver_init(struct DeviceDriver *driver) {
	if (cpu_check_feature(CPUID_FEAT_X2APIC)) {
		apic_info.apic_type = APIC_TYPE_X2APIC;
		// 还未实现x2apic支持，这里只是简单的初始化
		uint32_t low, high;
		read_msr(APIC_BASE_MSR, &low, &high);

		apic_info.apic_base		 = 0xfee00000;
		apic_info.apic_base_high = low >> 12;

		uint32_t tmp;
		DRV_RESULT_PRINT_CALL(
			driver_remap_memory, &apic_driver, apic_info.apic_base, 0x3ff,
			&tmp);
		apic_info.lapic_mmio = (uint32_t *)tmp;
		DRV_RESULT_PRINT_CALL(
			driver_remap_memory, &apic_driver, 0xfec00000, 0xfff00, &tmp);
		apic_info.ioapic = (struct ioapic *)tmp;

		read_msr(X2APIC_ID_MSR, &apic_info.apic_id, &apic_info.apic_id_high);
		apic_info.version = (lapic_read(APIC_Ver) & 0xff) |
							((lapic_read(APIC_Ver) >> 16) & 0xff);
	} else if (cpu_check_feature(CPUID_FEAT_APIC)) {
		apic_info.apic_type = APIC_TYPE_XAPIC;
		apic_info.apic_base = 0xfee00000;

		DRV_RESULT_PRINT_CALL(
			driver_remap_memory, &apic_driver, apic_info.apic_base, 0x3ff,
			(uint32_t *)&apic_info.lapic_mmio);
		DRV_RESULT_PRINT_CALL(
			driver_remap_memory, &apic_driver, 0xfec00000, 0xfff00,
			(uint32_t *)&apic_info.ioapic);

		apic_info.apic_id		= lapic_read(APIC_ID) >> 24;
		apic_info.version		= (lapic_read(APIC_Ver) & 0xff);
		apic_info.max_lvt_entry = (lapic_read(APIC_Ver) >> 16) & 0xff;
	} else {
		return DRIVER_RESULT_DEVICE_NOT_EXIST;
	}
	return DRIVER_RESULT_OK;
}

void enable_apic_with_msr(void) {
	// TODO: 完善xAPIC和x2APIC支持
	uint32_t low, high;
	read_msr(APIC_BASE_MSR, &low, &high);
	write_msr(APIC_BASE_MSR, BIN_EN(low, BIT(APIC_GLOBAL_ENABLE_BIT)), high);
}

void disable_apic_with_msr(void) {
	// TODO: 完善xAPIC和x2APIC支持
	uint32_t low, high;
	read_msr(APIC_BASE_MSR, &low, &high);
	write_msr(APIC_BASE_MSR, BIN_DIS(low, BIT(APIC_GLOBAL_ENABLE_BIT)), high);
}

void enable_apic_with_sivr(void) {
	uint32_t data = lapic_read(APIC_SIVR);
	lapic_write(APIC_SIVR, BIN_EN(data, BIT(APIC_SOFTWARE_ENABLE_BIT)));
}

void disable_apic_with_sivr(void) {
	uint32_t data = lapic_read(APIC_SIVR);
	lapic_write(APIC_SIVR, BIN_DIS(data, BIT(APIC_SOFTWARE_ENABLE_BIT)));
}

void enable_apic(void) {
	if (apic_info.apic_type == APIC_TYPE_X2APIC) {
		enable_apic_with_msr();
	} else if (apic_info.apic_type == APIC_TYPE_XAPIC) {
		enable_apic_with_sivr();
	}
}

void disable_apic(void) {
	if (apic_info.apic_type == APIC_TYPE_X2APIC) {
		disable_apic_with_msr();
	} else if (apic_info.apic_type == APIC_TYPE_XAPIC) {
		disable_apic_with_sivr();
	}
}

DriverResult apic_init(Device *device) {
	apic_driver_init(&apic_device_driver);

	lapic_write(APIC_LVT_LINT0, BIT(16));
	lapic_write(APIC_LVT_LINT1, BIT(16));
	lapic_write(APIC_LVT_ERROR, 0xfe);
	lapic_write(APIC_ESR, 0);
	lapic_write(APIC_ESR, 0);
	lapic_write(APIC_EOI, 0);

	lapic_write(APIC_ICR_HIGH, 0);
	lapic_write(
		APIC_ICR_LOW, APIC_ICR_DELIVERY_MODE_INIT |
						  APIC_ICR_TRIGGER_MODE_LEVEL |
						  APIC_ICR_DEST_SHORTHAND_ALL_INCLUDING_SELF);
	const int timeout = 100000;
	int		  i;
	for (i = 0;
		 i < timeout && lapic_read(APIC_ICR_LOW) & APIC_ICR_STAT_SEND_PENDING;
		 i++)
		;

	if (i == timeout) {
		print_error("APIC init timeout\n");
		return DRIVER_RESULT_TIMEOUT;
	}

	lapic_write(APIC_TPR, 0);

	for (int i = 0; i < apic_info.max_lvt_entry; i++) {
		apic_disable_irq(device->driver_manager_extension, 0x20 + i);
	}
	return DRIVER_RESULT_OK;
}

DriverResult apic_timer_calibrate(Device *device) {
	const int ms = 10;
	Timer	  timer;
	timer_init(&timer);

	uint32_t data = lapic_read(APIC_LVT_TIMER);
	lapic_write(APIC_LVT_TIMER, BIN_DIS(data, BIT(16)));

	lapic_write(APIC_TIMER_ICT, 0xffffffff);

	enable_interrupt();
	delay_ms(&timer, ms);
	disable_interrupt();

	lapic_write(APIC_LVT_TIMER, BIN_EN(data, BIT(16)));

	uint32_t apic_timer_count = lapic_read(APIC_TIMER_CCT);
	uint32_t freq			  = (0xffffffff - apic_timer_count) * (1000 / ms);
	apic_timer_timer_device.source_frequency = freq;
	apic_timer_timer_device.min_frequency	 = DIV_ROUND_UP(freq, 0xffffffff);
	apic_timer_timer_device.max_frequency	 = freq;
	return DRIVER_RESULT_OK;
}

DriverResult apic_timer_init(Device *device) {
	lapic_write(APIC_TIMER_DCR, APIC_TIMER_DCR_DIVIDE_BY_1);
	lapic_write(
		APIC_LVT_TIMER,
		APIC_LVT_TIMER_MODE_PERIODIC | (0x20 + LAPIC_TIMER_IRQ));
	apic_timer_calibrate(device);
	return DRIVER_RESULT_OK;
}

DriverResult apic_start(Device *device) {
	/*打了个洞，不过都有APIC了至少得有PIC吧，问题不大
	所有文档都指明了要先关闭8259A，那就关吧*/
	mask_8259a();

	enable_apic();
	return DRIVER_RESULT_OK;
}

DriverResult apic_timer_start(Device *device) {
	uint32_t data = lapic_read(APIC_LVT_TIMER);
	lapic_write(APIC_LVT_TIMER, BIN_DIS(data, BIT(16)));
	return DRIVER_RESULT_OK;
}

DriverResult apic_stop(Device *device) {
	disable_apic();
	return DRIVER_RESULT_OK;
}

DriverResult apic_timer_stop(Device *device) {
	uint32_t data = lapic_read(APIC_LVT_TIMER);
	lapic_write(APIC_LVT_TIMER, BIN_EN(data, BIT(16)));
	return DRIVER_RESULT_OK;
}

int apic_redirect_irq(InterruptDevice *device, int irq) {
	if (irq == PIC_PIT_IRQ) {
		irq = APIC_PIT_IRQ;
	} else if (irq == 2) {
		irq = 0;
	}
	return irq;
}

DriverResult apic_enable_irq(InterruptDevice *device, int irq) {
	io_apic_write(IOAPIC_TBL + irq * 2, BIN_DIS(0x20 + irq, BIT(16)));
	io_apic_write(IOAPIC_TBL + irq * 2 + 1, 0);
	return DRIVER_RESULT_OK;
}

DriverResult apic_disable_irq(InterruptDevice *device, int irq) {
	io_apic_write(IOAPIC_TBL + irq * 2, BIN_EN(0x20 + irq, BIT(16)));
	io_apic_write(IOAPIC_TBL + irq * 2 + 1, 0);
	return DRIVER_RESULT_OK;
}

void apic_eoi(InterruptDevice *device, int irq) {
	lapic_write(APIC_EOI, 0);
}

TimerResult apic_timer_set_frequency(
	TimerDevice *timer_device, uint32_t frequency) {
	uint32_t divisor = apic_timer_timer_device.source_frequency / frequency;
	lapic_write(APIC_TIMER_ICT, divisor);
	return TIMER_RESULT_OK;
}

void apic_timer_irq_handler(Device *device) {
	timer_irq_handler(device);
}

// --------old---------
// TODO: 删除旧代码

void init_apic(void) {
	// if (check_apic_support()) {
	// 	init_8259a();
	// 	return;
	// }
	// mask_8259a();
	use_apic = 1;
	// cpu_RDMSR(IA32_APIC_BASE, &l, &h);
	// printk("APIC Base:%#08x%08x\n", h, l);
	// cpu_WRMSR(IA32_APIC_BASE, l | (1<<10) | (1<<11), h);
	// //APIC全局使能，启用APIC
	// apic_info.lapic_mmio = remap(0xfee00000, 0x3ff);
	// apic_info.ioapic	 = remap(0xfec00000, 0xfff00);
	io_cli();
	lapic_write(APIC_SIVR, 1 << 8);
	// 设定Loacl APIC定时器
	lapic_write(APIC_TIMER_DCR, 0x0b); // divide by 16
	lapic_write(
		APIC_LVT_TIMER, (1 << 17) | (0x20 + LAPIC_TIMER_IRQ)); // 周期性计时
	int a, b, c, d;
	get_cpuid(
		0x15, 0x00, (unsigned int *)&a, (unsigned int *)&b, (unsigned int *)&c,
		(unsigned int *)&d);

	lapic_write(APIC_TIMER_ICT, 100000);

	// 获取APICID
	/*
	 * xAPIC Mode(Address:FEE0 0020H)
	 * P6 family and Pentium processors: 24~27bit
	 * Pentium 4 processors, Xeon processors, and later processors: 24~31bit
	 *
	 * x2APIC Mode(MSR Address: 802H):0~31bit
	 */
	int apicid = lapic_read(APIC_ID);
	printk("APICID:%#x \n", apicid);
	// 获取APIC版本
	/*
	 * 16~23位 Max LVT Entry
	 * 0~7位   Version
	 */
	printk("APIC Ver:%04x \n", lapic_read(APIC_Ver));
	// 屏蔽LVT
	//  lapic_write(APIC_LVT_CMCI, 1<<16);
	//  lapic_write(APIC_LVT_THMR, 1<<16);
	if (((apic_info.lapic_mmio[APIC_Ver / 4] >> 16) & 0xff) >= 4) {
		lapic_write(APIC_LVT_PMCR, 1 << 16);
	}
	lapic_write(APIC_LVT_LINT0, 1 << 16);
	lapic_write(APIC_LVT_LINT1, 1 << 16);
	lapic_write(APIC_LVT_ERROR, 0xfe);
	lapic_write(APIC_ESR, 0);
	lapic_write(APIC_ESR, 0);
	lapic_write(APIC_EOI, 0);

	lapic_write(APIC_ICR_HIGH, 0);
	lapic_write(APIC_ICR_LOW, (1 << 19) | (1 << 15) | (1 << 10) | (1 << 8));
	while (apic_info.lapic_mmio[APIC_ICR_LOW / 4] & (1 << 12))
		;

	lapic_write(APIC_TPR, 0);

	int i, count = (io_apic_read(IOAPIC_VER) >> 16) & 0xff;
	for (i = 0; i <= count; i++) {
		io_apic_write(IOAPIC_TBL + i * 2, APIC_INT_DISABLE | (0x20 + i));
		io_apic_write(IOAPIC_TBL + i * 2 + 1, 0);
	}

	put_irq_handler(LAPIC_TIMER_IRQ, (irq_handler_t)apic_timer_handler);
	// apic_enable_irq(LAPIC_TIMER_IRQ);
}

void apic_timer_handler(void) {
	struct task_s *cur_thread = get_current_thread();
	cur_thread->elapsed_ticks++;

	if (cur_thread->ticks == 0) {
		schedule();
	} else {
		cur_thread->ticks--;
	}
}
