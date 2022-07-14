#include <drivers/video.h>
#include <drivers/pit.h>
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
#include <kernel/app.h>
#include <config.h>

int main()
{
	int i, j;
	char *data;
	// init_page();
	init_descriptor();
	init_video();
	init_console();
	init_memory();
	INIT_PIC();
	init_timer();
	init_task();
	init_pci();
	io_sti();
	printk("Memory Size:%d\n", get_memory_size());
	printk("display mode: %d*%d %dbit\n", VideoInfo.width, VideoInfo.height, VideoInfo.BitsPerPixel);
	init_vfs();
	do_initcalls();
	init_fs();
	console_start();
	run_app("/hd0p1/apps/test");
	for(;;)
	{
		io_hlt();
	}
}