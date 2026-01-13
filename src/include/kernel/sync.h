#ifndef _SYNC_H
#define _SYNC_H

#include "kernel/wait_queue.h"
#include <stdint.h>

struct semaphore {
	uint8_t	  value;
	WaitQueue wq;
};

struct lock {
	struct task_s	*holder;
	struct semaphore semaphore;
	uint32_t		 holder_repeat_nr;
};

void lock_init(struct lock *plock);
void lock_acquire(struct lock *plock);
void lock_release(struct lock *plock);

void sema_init(struct semaphore *psema, uint8_t value);
void sema_down(struct semaphore *psema);
void sema_up(struct semaphore *psema);

#endif