/**
 * @file main.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 内核主程序
 * @date 2020-03
 */
#include "kernel/ards.h"
#include "kernel/memory/block.h"
#include "objects/transfer.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/exceptions.h>
#include <bits.h>
#include <driver/network/conn.h>
#include <driver/network/ethernet/ethernet.h>
#include <driver/network/protocols/ipv4/dhcp.h>
#include <driver/network/protocols/ipv4/ipv4.h>
#include <driver/network/protocols/protocols.h>
#include <driver/network/protocols/udp.h>
#include <driver/sound/pcm.h>
#include <driver/sound/sound_dm.h>
#include <driver/storage/storage_io_queue.h>
#include <drivers/vesa_display.h>
#include <fs/fs.h>
#include <kernel/app.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/periodic_task.h>
#include <kernel/platform.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <objects/handle.h>
#include <objects/ops.h>
#include <sections.h>
#include <stdint.h>
#include <string.h>

void run_memory_benchmarks(void);

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

void network_timer_init(void);

void kernel_early_init(void) {
	memory_early_init();
	platform_early_init();
}

int main() {
	uint8_t *zero = 0;

	init_memory();
	init_object_tree();
	init_device_managers();
	init_bus_manager();

	register_driver(&core_driver);

	platform_init();
	platform_start_devices();
	run_memory_benchmarks();
	while (true) {}

	init_task();
	task_idle = thread_start("Idle", 1, idle, 0, NULL);
	io_sti();
	printk(
		"Memory Size: Total %dKiB, Usable %dKiB\n", get_memory_total_size(),
		get_memory_usable_size());
	thread_start(
		"Kernel Periodic Tasks", THREAD_DEFAULT_PRIO, periodic_task, NULL,
		NULL);

	do_initcalls();
	driver_start_all();

	Object		*net;
	ObjectResult result = open_object_by_path("\\Device\\Network0", &net);
	if (result == OBJECT_OK) {
		dhcp_start(net->value.device.logical->dm_ext);
		// uint8_t dst_mac[]  = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		// uint8_t dst_ip[]   = {10, 0, 2, 2};
		// uint8_t udp_data[] = "Hello, World!";

		// NetworkConnection *conn = net_create_conn(net);
		// conn->buffer			= net_buffer_create(128);
		// net_buffer_init(conn->buffer, 128, 0, 0);

		// eth_register(conn);
		// ipv4_register(conn, NULL);
		// udp_register(conn);

		// // conn_put(conn, udp_data, sizeof(udp_data));

		// udp_wrap(conn, 1234, 22);
		// ipv4_wrap(conn, IP_PROTO_UDP, dst_ip, 64);
		// eth_wrap(conn, dst_mac, ETH_PROTO_TYPE_IPV4);
		// TRANSFER_OUT_STREAM(
		// 	net, conn->handle, conn->buffer->head,
		// 	conn->buffer->tail - conn->buffer->head);
	}

	// bios_emu_env.regs.ax		= 0x4f02;
	// bios_emu_env.regs.bx		= 0x4192;			   //
	// 1920x1080x32bit模式 BiosEmuExceptions exception =
	// emu_interrupt(0x10); // 调用BIOS 0x10中断 FrameBufferDevice
	// *fb_device; framebuffer_get_device(0, &fb_device);
	// fb_device->mode_info.width  = 1920;
	// fb_device->mode_info.height = 1080;
	// init_console(); // 重置控制台配置
	// if (exception == EventInterruptDone) {
	// 	printk("VBE Call Result: %d\n", bios_emu_env.regs.ax);
	// } else {
	// 	printk("VBE Error: %d\n", exception);
	// }

	// thread_start("play", 100, thread_play, NULL, NULL);

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
