#ifndef _CONDVAR_H
#define _CONDVAR_H

#include "kernel/spinlock.h"
#include "kernel/wait_queue.h"

typedef struct {
	WaitQueue wait_queue;
} condvar_t;

void condvar_init(condvar_t *cv);
void condvar_wait(condvar_t *cv, spinlock_t *mutex);
void condvar_signal(condvar_t *cv);
void condvar_broadcast(condvar_t *cv);

#endif
