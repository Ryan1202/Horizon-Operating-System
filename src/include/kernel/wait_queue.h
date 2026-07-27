#ifndef _WAIT_QUEUE_H
#define _WAIT_QUEUE_H

#include "kernel/list.h"
#include <kernel/spinlock.h>
#include <stdint.h>

// 为了避免额外的内存分配定义的结构，需通过接口使用，不得直接访问
typedef struct WaitQueue {
	list_t	   _list;
	uint8_t	   _initialized;
	spinlock_t _lock;
} WaitQueue;

typedef int (*wait_queue_try_fn)(void *context);

void wait_queue_init(WaitQueue *queue);

void wait_queue_wait(
	WaitQueue *queue, spinlock_t *condition_lock,
	wait_queue_try_fn try_condition, void *context);

void wait_queue_wake_one(WaitQueue *queue);
void wait_queue_wake_all(WaitQueue *queue);

#endif
