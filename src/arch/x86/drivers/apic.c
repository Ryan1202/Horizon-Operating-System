/**
 * @file apic.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief APIC驱动(参考xv6)
 * @version 0.1 Alpha
 * @date 2021-06
 */
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/cmos.h>
#include <drivers/msr.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/page.h>
#include <kernel/thread.h>

void apic_timer_handler(void);

uint32_t *lapic;
char	  use_apic;

volatile struct ioapic {
	uint32_t reg;
	uint32_t pad[3];
	uint32_t data;
} *ioapic;

// 检查是否支持x2APIC
int check_apic_support(void) {
	uint32_t a, b, c, d;
	get_cpuid(1, 0, &a, &b, &c, &d);
	printk("eax;%x ebx:%x ecx:%x edx:%x\n", a, b, c, d);
	if (!(d & (1 << 9))) {
		printk(COLOR_RED "No Support APIC!\n");
		return -1;
	}
	return 0;
}

uint32_t io_apic_read(uint32_t reg) {
	ioapic->reg = reg;
	return ioapic->data;
}

void io_apic_write(uint32_t reg, uint32_t data) {
	ioapic->reg	 = reg;
	ioapic->data = data;
}

void lapic_write(int index, int value) {
	lapic[index / 4] = value;
	lapic[APIC_ID / 4];
}

uint32_t lapic_read(int index) {
	return lapic[index / 4];
}

void init_apic(void) {
	if (check_apic_support()) {
		init_8259a();
		return;
	}
	mask_8259a();
	use_apic = 1;
	// cpu_RDMSR(IA32_APIC_BASE, &l, &h);
	// printk("APIC Base:%#08x%08x\n", h, l);
	// cpu_WRMSR(IA32_APIC_BASE, l | (1<<10) | (1<<11), h); //APIC全局使能，启用APIC
	lapic  = remap(0xfee00000, 0x3ff);
	ioapic = remap(0xfec00000, 0xfff00);
	io_cli();
	lapic_write(APIC_SIVR, 1 << 8);
	// 设定Loacl APIC定时器
	lapic_write(APIC_TIMER_DCR, 0x0b);								   // divide by 16
	lapic_write(APIC_LVT_TIMER, (1 << 17) | (0x20 + LAPIC_TIMER_IRQ)); // 周期性计时
	int a, b, c, d;
	get_cpuid(0x15, 0x00, (unsigned int *)&a, (unsigned int *)&b, (unsigned int *)&c, (unsigned int *)&d);

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
	if (((lapic[APIC_Ver / 4] >> 16) & 0xff) >= 4) { lapic_write(APIC_LVT_PMCR, 1 << 16); }
	lapic_write(APIC_LVT_LINT0, 1 << 16);
	lapic_write(APIC_LVT_LINT1, 1 << 16);
	lapic_write(APIC_LVT_ERROR, 0xfe);
	lapic_write(APIC_ESR, 0);
	lapic_write(APIC_ESR, 0);
	lapic_write(APIC_EOI, 0);

	lapic_write(APIC_ICR_HIGH, 0);
	lapic_write(APIC_ICR_LOW, (1 << 19) | (1 << 15) | (1 << 10) | (1 << 8));
	while (lapic[APIC_ICR_LOW / 4] & (1 << 12))
		;

	lapic_write(APIC_TPR, 0);

	int i, count = (io_apic_read(IOAPIC_VER) >> 16) & 0xff;
	for (i = 0; i <= count; i++) {
		io_apic_write(IOAPIC_TBL + i * 2, APIC_INT_DISABLE | (0x20 + i));
		io_apic_write(IOAPIC_TBL + i * 2 + 1, 0);
	}

	put_irq_handler(LAPIC_TIMER_IRQ, (irq_handler_t)apic_timer_handler);
	apic_enable_irq(LAPIC_TIMER_IRQ);
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

void apic_enable_irq(int irq) {
	io_apic_write(IOAPIC_TBL + irq * 2, 0x20 + irq);
	io_apic_write(IOAPIC_TBL + irq * 2 + 1, 0);
}

void apic_eoi(void) {
	lapic_write(APIC_EOI, 0);
}