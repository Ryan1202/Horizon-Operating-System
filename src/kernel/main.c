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
#include <network/dhcp.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/tcp.h>
#include <network/udp.h>

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

	int ret = dhcp_main(default_net_dev);
	while (ret == -4) {
		ret = dhcp_main(default_net_dev);
	}
	if (ret < 0) { printk("[DHCP]ipv4 address request failed!\n"); }

	uint8_t dst_ip[4] = {180, 101, 50, 188}, *router_mac;
	netc_t *netc	  = netc_create(default_net_dev, ETH_TYPE_ARP, 0);
	netc_set_dest(netc, broadcast_mac, NULL, 0);
	router_mac = ip2mac(netc, ((struct ipv4_data *)netc->net_dev->info->ipv4_data)->router_ip);
	netc_delete(netc);

	netc = netc_create(default_net_dev, ETH_TYPE_IPV4, PROTOCOL_TCP);
	netc_set_dest(netc, router_mac, dst_ip, 4);
	tcp_create(netc);
	tcp_bind(netc, dst_ip, 12345, 80);
	tcp_ipv4_connect(netc);
	tcp_ipv4_close(netc);

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
