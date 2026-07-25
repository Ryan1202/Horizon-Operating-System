#ifndef _COMPLETION_H
#define _COMPLETION_H

#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <types.h>

typedef struct CCompletion {
	size_t     state;
	spinlock_t state_lock;
	WaitQueue *wait_queue;
} CCompletion;

bool completion_init(CCompletion *completion);
void completion_deinit(CCompletion *completion);
void completion_wait(CCompletion *completion);
void completion_complete(CCompletion *completion);

#endif
