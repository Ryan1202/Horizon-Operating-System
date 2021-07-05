#ifndef _MOUSE_H
#define _MOUSE_H

#include <kernel/fifo.h>

#define MOUSE_IRQ 12

typedef void mouse_handler(int);

struct mouse
{
	int x, y;
	int old_x, old_y;
	int lbtn, mbtn, rbtn;		//左键 中键 右键
};

extern struct fifo mouse_fifo;
extern struct mouse mouse;

void init_mouse(mouse_handler *handler);

#endif