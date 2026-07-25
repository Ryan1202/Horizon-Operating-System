#ifndef _WAIT_QUEUE_H
#define _WAIT_QUEUE_H

#include <kernel/spinlock.h>
#include <stddef.h>

typedef struct WaitQueue WaitQueue;
typedef int (*wait_queue_try_fn)(void *context);

WaitQueue *wait_queue_create(void);
void       wait_queue_destroy(WaitQueue *queue);

void wait_queue_wait(
	WaitQueue *queue, spinlock_t *condition_lock,
	wait_queue_try_fn try_condition, void *context);

void wait_queue_wake_one(WaitQueue *queue);
void wait_queue_wake_all(WaitQueue *queue);

#endif
