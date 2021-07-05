#include <device/video.h>
#include <kernel/page.h>
#include <kernel/font.h>
#include <kernel/descriptor.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <kernel/fs/fs.h>
#include <kernel/thread.h>
#include <kernel/func.h>
#include <device/8259a.h>
#include <device/pit.h>
#include <device/keyboard.h>
#include <device/pci.h>
#include <device/ide.h>
#include <device/smbios.h>
#include <device/cpufreq.h>
#include <device/msr.h>
#include <device/acpi.h>
#include <device/apic.h>
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
	// while(1);
	init_memory();
	// init_apic();
	// init_8259a();
	INIT_PIC();
	init_task();
	init_timer();
	io_sti();
	printk("Memory Size:%d\n", get_memory_size());
	printk("display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel);
	init_keyboard(0);
	init_pci();
	init_ide();
	init_acpi();
	console_start();
	// init_smbios();
	thread_start("Kthread_A", 1000, k_thread_a, "A ");
	thread_start("Kthread_B", 1000, k_thread_b, "B ");
	for(;;) {
		if (fifo_status(&keyfifo))
		{
			console_input(scancode_analysis(fifo_get(&keyfifo)));
		}
		io_hlt();
	}
}