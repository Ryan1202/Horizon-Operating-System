/**
 * @file apic.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief APIC驱动(参考xv6)
 * @version 0.1
 * @date 2025-09
 */
#include <bits.h>
#include <driver/interrupt/interrupt_dm.h>
#include <driver/timer/timer_dm.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/cmos.h>
#include <drivers/msr.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/feature.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/page.h>
#include <kernel/platform.h>
#include <kernel/thread.h>
#include <math.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

DriverResult apic_init(void *device);
DriverResult apic_start(void *device);
DriverResult apic_stop(void *device);
DriverResult apic_driver_init(struct DeviceDriver *driver);
int			 apic_redirect_irq(InterruptDevice *device, int irq);
DriverResult apic_enable_irq(InterruptDevice *device, int irq);
DriverResult apic_disable_irq(InterruptDevice *device, int irq);
DriverResult apic_timer_init(void *device);
DriverResult apic_timer_start(void *device);
DriverResult apic_timer_stop(void *device);
void		 apic_eoi(InterruptDevice *device, int irq);
TimerResult	 apic_timer_set_frequency(
	 TimerDevice *timer_device, uint32_t frequency);
void apic_timer_irq_handler(void *device);

extern Driver core_driver;

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
	DeviceIrq	  *device_irq;
} ApicInfo;
ApicInfo apic_info;

volatile struct ioapic {
	uint32_t reg;
	uint32_t pad[3];
	uint32_t data;
};

bool use_apic;

uint32_t lapic_write(int index, int value) {
	apic_info.lapic_mmio[index / 4] = value;
	return apic_info.lapic_mmio[APIC_ID / 4];
}

uint32_t lapic_read(int index) {
	return apic_info.lapic_mmio[index / 4];
}

DeviceOps apic_device_ops = {
	.init	 = apic_init,
	.start	 = apic_start,
	.stop	 = apic_stop,
	.destroy = NULL,
};
DeviceOps apic_interrupt_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};
DeviceOps apic_timer_device_ops = {
	.init	 = apic_timer_init,
	.start	 = apic_timer_start,
	.stop	 = apic_timer_stop,
	.destroy = NULL,
};
InterruptDeviceOps apic_interrupt_ops = {
	.enable_irq	  = apic_enable_irq,
	.disable_irq  = apic_disable_irq,
	.eoi		  = apic_eoi,
	.redirect_irq = apic_redirect_irq,
};
TimerOps apic_timer_ops = {
	.set_frequency = apic_timer_set_frequency,
};

DeviceDriver	 apic_device_driver;
PhysicalDevice	*apic_device;
InterruptDevice *apic_interrupt_device;
TimerDevice		*apic_timer_device;

uint32_t io_apic_read(uint32_t reg) {
	apic_info.ioapic->reg = reg;
	return apic_info.ioapic->data;
}

void io_apic_write(uint32_t reg, uint32_t data) {
	apic_info.ioapic->reg  = reg;
	apic_info.ioapic->data = data;
}

DriverResult register_apic(void) {
	register_device_driver(&core_driver, &apic_device_driver);

	apic_driver_init(&apic_device_driver);
	if (!use_apic) return DRIVER_ERROR_NOT_EXIST;

	ObjectAttr attr = device_object_attr;
	DRIVER_RESULT_PASS(
		create_physical_device(&apic_device, platform_bus, &attr));
	register_physical_device(apic_device, &apic_device_ops);

	DRIVER_RESULT_PASS(create_interrupt_device(
		&apic_interrupt_device, &apic_interrupt_device_ops, apic_device,
		&apic_device_driver, &apic_interrupt_ops, 1));
	return create_timer_device(
		&apic_timer_device, &apic_timer_ops, &apic_timer_device_ops,
		apic_device, &apic_device_driver);
}

void x2apic_init(struct DeviceDriver *driver) {
	apic_info.apic_type = APIC_TYPE_X2APIC;
	// 还未实现x2apic支持，这里只是简单的初始化
	uint32_t low, high;
	read_msr(APIC_BASE_MSR, &low, &high);

	apic_info.apic_base		 = 0xfee00000;
	apic_info.apic_base_high = low >> 12;

	uint32_t tmp;
	DRV_RESULT_PRINT_CALL(
		driver_remap_memory(&core_driver, apic_info.apic_base, 0x3ff, &tmp));
	apic_info.lapic_mmio = (uint32_t *)tmp;
	DRV_RESULT_PRINT_CALL(
		driver_remap_memory(&core_driver, 0xfec00000, 0xfff00, &tmp));
	apic_info.ioapic = (struct ioapic *)tmp;

	read_msr(X2APIC_ID_MSR, &apic_info.apic_id, &apic_info.apic_id_high);
	apic_info.version =
		(lapic_read(APIC_Ver) & 0xff) | ((lapic_read(APIC_Ver) >> 16) & 0xff);
}

void xapic_init(struct DeviceDriver *driver) {
	apic_info.apic_type = APIC_TYPE_XAPIC;
	apic_info.apic_base = 0xfee00000;

	DRV_RESULT_PRINT_CALL(driver_remap_memory(
		&core_driver, apic_info.apic_base, 0x3ff,
		(uint32_t *)&apic_info.lapic_mmio));
	DRV_RESULT_PRINT_CALL(driver_remap_memory(
		&core_driver, 0xfec00000, 0xfff00, (uint32_t *)&apic_info.ioapic));

	apic_info.apic_id		= lapic_read(APIC_ID) >> 24;
	apic_info.version		= (lapic_read(APIC_Ver) & 0xff);
	apic_info.max_lvt_entry = (lapic_read(APIC_Ver) >> 16) & 0xff;
}

DriverResult apic_driver_init(struct DeviceDriver *driver) {
	/**
	 * 所有文档都说要先屏蔽8259a的中断，直到我无数次触发#DF才知道为什么...
	 * 防止apic完成初始化前触发中断无法正确处理导致异常
	 */
	use_apic = true;
	if (cpu_check_feature(CPUID_FEAT_X2APIC)) {
		mask_8259a();
		x2apic_init(driver);
	} else if (cpu_check_feature(CPUID_FEAT_APIC)) {
		mask_8259a();
		xapic_init(driver);
	} else {
		use_apic = false;
		return DRIVER_ERROR_NOT_EXIST;
	}
	return DRIVER_OK;
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

DriverResult apic_init(void *device) {
	DRIVER_RESULT_PASS(apic_driver_init(&apic_device_driver));

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
		print_error_with_position("APIC init timeout\n");
		return DRIVER_ERROR_TIMEOUT;
	}

	lapic_write(APIC_TPR, 0);

	for (int i = 0; i < apic_info.max_lvt_entry; i++) {
		apic_disable_irq(apic_interrupt_device, 0x20 + i);
	}
	return DRIVER_OK;
}

DriverResult apic_timer_calibrate(LogicalDevice *device) {
	const int ms = 100;
	Timer	  timer;
	timer_init(&timer);

	uint32_t data = lapic_read(APIC_LVT_TIMER);
	lapic_write(APIC_LVT_TIMER, BIN_DIS(data, BIT(16)));

	lapic_write(APIC_TIMER_ICT, 0xffffffff);

	delay_ms(&timer, ms);

	lapic_write(APIC_LVT_TIMER, BIN_EN(data, BIT(16)));

	uint32_t apic_timer_count			 = lapic_read(APIC_TIMER_CCT);
	uint32_t delta						 = (0xffffffff - apic_timer_count);
	uint32_t freq						 = delta * (1000 / ms);
	apic_timer_device->source_frequency	 = freq;
	apic_timer_device->min_frequency	 = DIV_ROUND_UP(freq, 0xffffffff);
	apic_timer_device->max_frequency	 = freq;
	apic_timer_device->current_frequency = 0;
	apic_timer_device->priority			 = 2;
	return DRIVER_OK;
}

DriverResult apic_timer_init(void *device) {
	lapic_write(APIC_TIMER_DCR, APIC_TIMER_DCR_DIVIDE_BY_1);
	lapic_write(
		APIC_LVT_TIMER,
		APIC_LVT_TIMER_MODE_PERIODIC | (0x20 + LAPIC_TIMER_IRQ));

	apic_timer_calibrate(device);
	return DRIVER_OK;
}

DriverResult apic_start(void *device) {
	enable_apic();
	return DRIVER_OK;
}

DriverResult apic_timer_start(void *device) {
	register_device_irq(
		&apic_info.device_irq, apic_device, device, LAPIC_TIMER_IRQ,
		apic_timer_irq_handler, IRQ_MODE_EXCLUSIVE);
	enable_device_irq(apic_info.device_irq);
	uint32_t data = lapic_read(APIC_LVT_TIMER);
	lapic_write(APIC_LVT_TIMER, BIN_DIS(data, BIT(16)));
	return DRIVER_OK;
}

DriverResult apic_stop(void *device) {
	disable_apic();
	return DRIVER_OK;
}

DriverResult apic_timer_stop(void *device) {
	uint32_t data = lapic_read(APIC_LVT_TIMER);
	lapic_write(APIC_LVT_TIMER, BIN_EN(data, BIT(16)));
	unregister_device_irq(apic_info.device_irq);
	apic_info.device_irq = NULL;
	return DRIVER_OK;
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
	return DRIVER_OK;
}

DriverResult apic_disable_irq(InterruptDevice *device, int irq) {
	io_apic_write(IOAPIC_TBL + irq * 2, BIN_EN(0x20 + irq, BIT(16)));
	io_apic_write(IOAPIC_TBL + irq * 2 + 1, 0);
	return DRIVER_OK;
}

void apic_eoi(InterruptDevice *device, int irq) {
	lapic_write(APIC_EOI, 0);
}

TimerResult apic_timer_set_frequency(
	TimerDevice *timer_device, uint32_t frequency) {
	uint32_t divisor =
		DIV_ROUND_UP(apic_timer_device->source_frequency, frequency);
	lapic_write(APIC_TIMER_ICT, divisor);
	return TIMER_RESULT_OK;
}

void apic_timer_irq_handler(void *device) {
	timer_irq_handler(device);
}
