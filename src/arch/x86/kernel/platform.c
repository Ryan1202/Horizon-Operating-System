#include <drivers/apic.h>
#include <drivers/pit.h>
#include <drivers/video.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>

void platform_init() {
	init_descriptor();
	init_video();
	init_console();
	init_memory();
	init_apic();
	init_timer();
}