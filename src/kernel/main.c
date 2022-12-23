/**
 * @file main.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内核主程序
 * @version 0.9
 * @date 2020-03
 */
#include <drivers/video.h>
#include <drivers/pit.h>
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <kernel/page.h>
#include <kernel/font.h>
#include <kernel/descriptor.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <kernel/thread.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/driver.h>
#include <drivers/pci.h>
#include <kernel/process.h>
#include <kernel/app.h>
#include <fs/fs.h>
#include <fs/vfs.h>

void idle(void *arg);

int main()
{
	int i;
	init_descriptor();
	init_video();
	init_console();
	init_memory();
	init_apic();
	init_timer();
	init_task();
	init_pci();
	io_sti();
	printk("Memory Size:%d\n", get_memory_size());
	printk("display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel);
	thread_start("Idle", 1, idle, 0);
	init_vfs();
	do_initcalls();
	init_fs();
	console_start();
	for(;;)
	{
		io_hlt();
	}
}

void idle(void *arg)
{
	for(;;)
	{
		io_hlt();
	}
}
