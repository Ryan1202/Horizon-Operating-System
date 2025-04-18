/**
 * @file main.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内核主程序
 * @date 2020-03
 */
#include "bios_emu/bios_emu.h"
#include "bios_emu/exceptions.h"
#include "driver/sound/pcm.h"
#include "driver/sound/sound_dm.h"
#include "drivers/vesa_display.h"
#include "kernel/page.h"
#include "objects/handle.h"
#include "string.h"
#include <driver/interrupt_dm.h>
#include <driver/storage/disk/volume.h>
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io_queue.h>
#include <driver/time_dm.h>
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
#include <kernel/list.h>
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
#include <objects/object.h>
#include <objects/ops.h>
#include <objects/transfer.h>
#include <stdint.h>

void		   idle(void *arg);
struct task_s *task_idle;
extern Driver  core_driver;

// void print_permission(Permission *permission) {
// 	printk("Subject ID:%d\n", permission->subject_id);
// 	printk("Permission:");
// 	if (permission->permission.visible) { printk("Visible "); }
// 	if (permission->permission.read) { printk("Read "); }
// 	if (permission->permission.write) { printk("Write "); }
// 	if (permission->permission.execute) { printk("Execute "); }
// 	if (permission->permission.delete) { printk("Delete "); }
// 	if (permission->permission.rename) { printk("Rename "); }
// 	if (permission->permission.set_attr) { printk("SetAttr "); }
// 	printk("\n");
// }

void thread_play(void *arg) {
	Object		*object;
	ObjectResult result = open_object_by_path("\\Device\\Sound0", &object);
	if (result != OBJECT_OK) {
		printk("Open File Error!\n");
	} else {
		Object *file;
		result =
			open_object_by_path("\\Volumes\\Storage0Volume0\\1.pcm", &file);

		if (result == OBJECT_OK) {
			ObjectHandle *handle = object_handle_create(file);
			PcmDevice	 *pcm;
			PcmStream	 *stream;
			sound_pcm_open(object, SOUND_DEVICE_MODE_PLAY, &pcm, &stream);
			sound_pcm_alloc(stream);
			pcm_set_sample_rate(pcm, 44100);
			pcm_set_channel(stream, 2);
			sound_pcm_set_frame_count(stream, 4 * 1024);
			sound_pcm_prepare(stream);
			ObjectAttr attr;
			obj_get_attr(file, &attr);
			size_t	 count = 16 * 64;
			size_t	 size  = 18 * 1024 * 1024;
			uint8_t *buf   = kmalloc(19 * 1024 * 1024);
			for (int i = 0; i < size / 1024 / 1024; i++) {
				TransferResult result = TRANSFER_IN_STREAM(
					file, handle, buf + i * 1024 * 1024, 1024 * 1024);
				if (result != TRANSFER_OK) {
					printk("Transfer Error!\n");
					thread_exit();
				}
			}
			for (int i = 0; i < count; i++) {
				sound_pcm_write(stream, buf, 4 * 1024);
				buf += 16 * 1024;
			}
		}
	}
}

int main() {
	platform_early_init();

	init_memory();

	uint8_t *zero = 0;

	register_driver_manager(&device_driver_manager);
	register_driver_manager(&bus_driver_manager);
	register_device_manager(&interrupt_dm);
	register_device_manager(&timer_dm);
	register_device_manager(&time_dm);
	register_device_manager(&video_dm);
	register_device_manager(&sound_dm);
	register_device_manager(&storage_dm);

	init_object_tree();

	register_driver(&core_driver);
	driver_init(&core_driver);

	platform_init();
	platform_start_devices();

	init_task();
	task_idle = thread_start("Idle", 1, idle, 0, NULL);
	io_sti();
	printk("Memory Size:%dM\n", get_memory_size());
	thread_start(
		"Kernel Periodic Tasks", THREAD_DEFAULT_PRIO, periodic_task, NULL,
		NULL);

	do_initcalls();
	driver_start_all();

	// bios_emu_env.regs.ax		= 0x4f02;
	// bios_emu_env.regs.bx		= 0x4192;			   // 1920x1080x32bit模式
	// BiosEmuExceptions exception = emu_interrupt(0x10); // 调用BIOS 0x10中断
	// VideoDevice		 *video_device;
	// video_get_video_device(0, &video_device);
	// video_device->mode_info.width  = 1920;
	// video_device->mode_info.height = 1080;
	// init_console(); // 重置控制台配置
	// if (exception == EventInterruptDone) {
	// 	printk("VBE Call Result: %d\n", bios_emu_env.regs.ax);
	// } else {
	// 	printk("VBE Error: %d\n", exception);
	// }

	// thread_start("play", 100, thread_play, NULL, NULL);

	// const string_t name = STRING_INIT("A folder");
	// obj_rmdir(object, name);

	// void		*handle = NULL;
	// Object		*object;
	// ObjectResult result =
	// 	open_object_by_ascii_path("\\Device\\Storage0\\Partition0",
	// &object);
	// if (result != OBJECT_OK) {
	// 	}

	// bool is_done;
	// do {
	// 	TRANSFER_IN_IS_DONE(object)(object, &handle, &is_done);
	// } while (!is_done);

	// show_object_tree();

	// thread_start(
	// 	"NetworkRxPacketProcess", THREAD_DEFAULT_PRIO, net_process_pack,
	// NULL);

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
	// 	netc, ((struct ipv4_data
	// *)netc->net_dev->info->ipv4_data)->router_ip); netc_delete(netc);

	// netc = netc_create(default_net_dev, ETH_TYPE_IPV4, PROTOCOL_TCP);
	// netc_set_dest(netc, router_mac, dst_ip, 4);
	// tcp_create(netc);
	// tcp_bind(netc, 12345);
	// tcp_ipv4_connect(netc, dst_ip, 80);
	// uint8_t	 data[] = "GET / HTTP/1.1\r\nHost:
	// 180.101.50.188\r\nAccept: "
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

	thread_exit();
	for (;;) {
		io_hlt();
	}
}

void idle(void *arg) {
	for (;;) {
		schedule();
	}
}
