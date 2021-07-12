#include <drivers/video.h>
#include <kernel/page.h>
#include <kernel/font.h>
#include <kernel/descriptor.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <fs/fs.h>
#include <kernel/thread.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/driver.h>
// #include <drivers/8259a.h>
// #include <drivers/pit.h>
// #include <drivers/keyboard.h>
#include <drivers/pci.h>
// #include <drivers/ide.h>
// #include <drivers/smbios.h>
// #include <drivers/cpufreq.h>
// #include <drivers/msr.h>
// #include <drivers/acpi.h>
// #include <drivers/apic.h>
#include <config.h>

void k_thread_a(void *arg)
{
	char *para = arg;
	while (1)
	{
		io_hlt();
	}
}

void k_thread_b(void *arg)
{
	char *para = arg;
	while (1)
	{
		io_hlt();
	}
}

int main()
{
	int i, j;
	char *data;
	init_page();
	init_descriptor();
	init_video();
	init_console();
	init_memory();
	INIT_PIC();
	init_task();
	init_timer();
	io_sti();
	printk("Memory Size:%d\n", get_memory_size());
	printk("display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel);
	init_pci();
	do_initcalls();
	printk("Registered Drivers:\n");
	show_drivers();
	console_start();
	// thread_start("Kthread_A", 1000, k_thread_a, "A ");
	// thread_start("Kthread_B", 1000, k_thread_b, "B ");
	for(;;) {
		// if (fifo_status(&keyfifo))
		// {
			// console_input(scancode_analysis(fifo_get(&keyfifo)));
		// }
		io_hlt();
	}
}