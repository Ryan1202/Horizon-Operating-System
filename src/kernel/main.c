/**
 * @file main.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内核主程序
 * @version 0.9
 * @date 2020-03
 */
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/cmos.h>
#include <drivers/pci.h>
#include <drivers/pit.h>
#include <drivers/video.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/app.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/font.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <network/arp.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/udp.h>

void				   idle(void *arg);
extern struct timerctl timerctl;
struct task_s		  *l;

int main() {
	init_descriptor();
	init_video();
	init_console();
	init_memory();
	init_apic();
	init_timer();
	init_task();
	l = thread_start("Idle", 1, idle, 0);
	init_pci();
	io_sti();
	printk("Memory Size:%d\n", get_memory_size());
	printk("Display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel);
	init_vfs();
	do_initcalls();
	init_fs();

	uint8_t dst_ip[] = {192, 168, 1, 1}, src_ip[] = {0, 0, 0, 0};
	uint8_t dst_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	uint8_t src_mac[6];
	uint8_t data[] = "Hello World!";
	DEV_CTL(default_net_device, NET_FUNC_GET_MAC_ADDR, src_mac);
	udp_send(default_net_device, src_mac, src_ip, dst_mac, dst_ip, 80, 80, (uint16_t *)data, sizeof(data));

	console_start();

	for (;;) {
		io_hlt();
	}
}

void idle(void *arg) {
	for (;;) {
		io_hlt();
	}
}
