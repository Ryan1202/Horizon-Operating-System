#ifndef _NETWORK_QUEUE_H
#define _NETWORK_QUEUE_H

#include <bits.h>
#include <stdint.h>
#define NQ_BLOCKER_DRIVER BIT(0)
#define NQ_BLOCKER_SYSTEM BIT(1)

typedef struct NetworkQueue {
	uint8_t blocker;
} NetworkQueue;

#endif