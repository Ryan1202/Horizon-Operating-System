/**
 * @file 8259a.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief PIC驱动
 * @version 1.1
 * @date 2022-07-31
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <drivers/apic.h>
#include <drivers/8259a.h>
#include <kernel/func.h>
#include <kernel/descriptor.h>

void init_8259a(void)
{
	io_out8(PIC0_IMR, 0xff);	//屏蔽主PIC的所有中断
	io_out8(PIC1_IMR, 0xff);	//屏蔽从PIC的所有中断
	
	io_out8(PIC0_ICW1, 0x11);	//级联，边沿触发
	io_out8(PIC0_ICW2, 0x20);	//起始中断向量号位0x20(0x00~0x1f为内部中断)
	io_out8(PIC0_ICW3, 1<<2);	//IRQ2用于连接从PIC
	io_out8(PIC0_ICW4, 0x01);	//全嵌套模式，非缓冲模式，手动结束中断，x86处理器
	
	io_out8(PIC1_ICW1, 0x11);	//级联，边沿触发
	io_out8(PIC1_ICW2, 0x28);	//起始中断向量号位0x28
	io_out8(PIC1_ICW3, 0x02);	//连接到主PIC的IRQ2
	io_out8(PIC1_ICW4, 0x01);	//全嵌套模式，非缓冲模式，手动结束中断，x86处理器
	
	io_out8(PIC0_IMR, 0xfb);	//开启IRQ2(从PIC)中断
	io_out8(PIC1_IMR, 0xff);	//屏蔽从PIC的所有中断
	
	use_apic = 0;
	
	return;
}

void mask_8259a(void)
{
	io_out8(PIC0_IMR, 0xff);
	io_out8(PIC1_IMR, 0xff);
}

void pic_enable_irq(int irq)
{
	uint8_t data;
	if (irq < 8)
	{
		data = io_in8(PIC0_IMR);
		io_out8(PIC0_IMR, data & ~(1 << irq));
	}
	else
	{
		data = io_in8(PIC1_IMR);
		io_out8(PIC1_IMR, data & ~(1 << (irq % 8)));
	}
}

void pic_eoi(int irq)
{
	if (irq >= 8)
	{
		io_out8(PIC1_OCW1, PIC_EOI);
	}
	io_out8(PIC0_OCW1, PIC_EOI);
}
