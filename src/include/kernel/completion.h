#ifndef _COMPLETION_H
#define _COMPLETION_H

#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <types.h>

typedef struct Completion {
	size_t	   state;
	spinlock_t state_lock;
	WaitQueue  wait_queue;
} Completion;

bool completion_init(Completion *completion);
void completion_wait(Completion *completion);
void completion_complete(Completion *completion);

#endif
