#ifndef _SYNC_H
#define _SYNC_H

#include "list.h"
#include <kernel/thread.h>
#include <stdint.h>

struct semaphore
{
    uint8_t value;
    struct list waiters;
};

struct lock
{
    struct task_s *holder;
    struct semaphore semaphore;
    uint32_t holder_repeat_nr;
};

void lock_init(struct lock *plock);
void lock_acquire(struct lock *plock);
void lock_release(struct lock *plock);

#endif