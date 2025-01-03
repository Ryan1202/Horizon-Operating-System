/**
 * @file main.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内核主程序
 * @date 2020-03
 */
#include <driver/interrupt_dm.h>
#include <driver/storage_dm.h>
#include <driver/timer_dm.h>
#include <driver/video_dm.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/app.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/driver_manager.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/periodic_task.h>
#include <kernel/platform.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <network/arp.h>
#include <network/dhcp.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/netpack.h>
#include <network/network.h>
#include <network/tcp.h>
#include <network/udp.h>

void		   idle(void *arg);
struct task_s *task_idle;

int main() {
	register_driver_manager(&device_driver_manager);
	register_driver_manager(&bus_driver_manager);
	register_device_manager(&interrupt_device_manager);
	register_device_manager(&timer_device_manager);
	register_device_manager(&video_device_manager);
	register_device_manager(&storage_device_manager);

	init_platform();
	platform_init_and_start_devices();

	init_task();
	task_idle = thread_start("Idle", 1, idle, 0);
	// init_pci();
	io_sti();
	printk("Memory Size:%dM\n", get_memory_size());
	// init_vfs();
	do_initcalls();
	driver_start_all();
	thread_start(
		"Kernel Periodic Tasks", THREAD_DEFAULT_PRIO, periodic_task, NULL);
	// init_fs();

	// thread_start(
	// 	"NetworkRxPacketProcess", THREAD_DEFAULT_PRIO, net_process_pack, NULL);

	// int ret = dhcp_main(default_net_dev);
	// while (ret == -4) {
	// 	ret = dhcp_main(default_net_dev);
	// }
	// if (ret < 0) { printk("[DHCP]ipv4 address request failed!\n"); }
	// struct ipv4_data *ipv4 = default_net_dev->info->ipv4_data;
	// memcpy(ipv4->ip_addr, (uint8_t[4]){10, 0, 2, 15}, 4);
	// memcpy(ipv4->router_ip, (uint8_t[4]){10, 0, 2, 2}, 4);

	// uint8_t dst_ip[4] = {180, 101, 50, 188}, *router_mac;
	// netc_t *netc	  = netc_create(default_net_dev, ETH_TYPE_ARP, 0);
	// netc_set_dest(netc, broadcast_mac, NULL, 0);
	// router_mac = ip2mac(
	// 	netc, ((struct ipv4_data *)netc->net_dev->info->ipv4_data)->router_ip);
	// netc_delete(netc);

	// netc = netc_create(default_net_dev, ETH_TYPE_IPV4, PROTOCOL_TCP);
	// netc_set_dest(netc, router_mac, dst_ip, 4);
	// tcp_create(netc);
	// tcp_bind(netc, 12345);
	// tcp_ipv4_connect(netc, dst_ip, 80);
	// uint8_t	 data[] = "GET / HTTP/1.1\r\nHost: 180.101.50.188\r\nAccept: "
	// 				  "*/*\r\nConnection: keep-alive\r\n\r\n";
	// uint8_t *rb		= kmalloc(2048);
	// tcp_write(netc, data, sizeof(data));
	// int len = 10499;
	// int i, tmp;
	// do {
	// 	tmp = tcp_read(netc, rb, 1152);
	// 	len -= tmp;
	// 	// for (i = 0; i < tmp; i++) {
	// 	// 	printk("%c", rb[i]);
	// 	// }
	// } while (len > 0);
	// printk("\nend.\n");
	// tcp_ipv4_close(netc);

	console_start();

	for (;;) {
		io_hlt();
	}
}

void idle(void *arg) {
	for (;;) {
		schedule();
	}
}
