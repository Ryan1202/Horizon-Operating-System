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
#include <drivers/pci.h>
#include <kernel/process.h>
#include <config.h>

int test_var_a = 0, test_var_b = 0;

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

void u_prog_a(void)
{
	while (1)
	{
		test_var_a++;
	}
}

void u_prog_b(void)
{
	while (1)
	{
		test_var_b++;
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
	init_vfs();
	do_initcalls();
	init_fs();
	console_start();
	thread_start("Kthread_A", THREAD_DEFAULT_PRIO, k_thread_a, "A ");
	thread_start("Kthread_B", THREAD_DEFAULT_PRIO, k_thread_b, "B ");
	process_excute(u_prog_a, "user_prog_a");
	process_excute(u_prog_b, "user_prog_b");	
	
	for(;;) {
		io_hlt();
	}
}