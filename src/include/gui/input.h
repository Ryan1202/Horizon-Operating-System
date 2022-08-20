#ifndef _INPUT_H
#define _INPUT_H

#include <stdint.h>

enum cursor_status {
	CSTAT_DEFAULT = 0,
	CSTAT_SELECT_LINK,
	CSTAT_SELECT_TEXT,
	CSTAT_MOVE,
	CSTAT_BUSY
};

struct input_devices {
	struct mouse {
		struct layer_s *layer;
		enum cursor_status status;
		int8_t lbtn, mbtn, rbtn;
		int8_t wait_ack;
		struct fifo *fifo;
		struct index_node *inode;
	}mouse;
	struct keyboard
	{
		int8_t lctrl, rctrl;
		int8_t lalt, ralt;
		int8_t lshift, rshift;
		int8_t num_lock, caps_lock, scroll_lock;
		struct fifo *fifo;
		struct index_node *inode;
	}keyboard;
};

void init_input(struct gui_s *gui);
void input_handler(struct gui_s *gui);

#endif