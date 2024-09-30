/**
 * @file main.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内核主程序
 * @version 0.9
 * @date 2020-03
 */
#include "network/netpack.h"
#include "stdint.h"
#include "string.h"
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/cmos.h>
#include <drivers/pci.h>
#include <drivers/pit.h>
#include <drivers/video.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <gui/gui.h>
#include <kernel/app.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/font.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/process.h>

void		   idle(void *arg);
struct task_s *task_idle;

int main() {
	init_descriptor();
	init_video();
	init_console();
	init_memory();
	init_apic();
	init_timer();
	init_task();
	task_idle = thread_start("Idle", 1, idle, 0);
	init_pci();
	io_sti();
	printk("Memory Size:%d\n", get_memory_size());
	printk("Display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel);
	init_vfs();
	do_initcalls();
	init_fs();

	thread_start("NetworkRxPacketProcess", THREAD_DEFAULT_PRIO, net_process_pack, NULL);

	thread_start("GUI Main", 100, gui_start, 0);

	for (;;) {
		io_hlt();
	}
}

void idle(void *arg) {
	for (;;) {
		io_hlt();
	}
}
