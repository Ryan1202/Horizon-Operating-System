#include <device/mouse.h>
#include <device/8042.h>
#include <device/apic.h>
#include <kernel/descriptor.h>
#include <kernel/func.h>
#include <config.h>

struct fifo mouse_fifo;
struct mouse mouse;

void init_mouse(mouse_handler *handler)
{
	mouse.x = 0;
	mouse.y = 0;
	mouse.old_x = 0;
	mouse.old_y = 0;
	mouse.lbtn = 0;
	mouse.mbtn = 0;
	mouse.rbtn = 0;
	
	put_irq_handler(MOUSE_IRQ, handler);
	// enable_irq(2);
	irq_enable(MOUSE_IRQ);
}
