#ifndef _MUTEX_H
#define _MUTEX_H

#include "kernel/thread.h"

typedef volatile int mutex_t;

void mutex_init(mutex_t *lock) {
	*lock = 0;
}

void mutex_lock(mutex_t *lock) {
	while (__sync_lock_test_and_set(lock, 1)) {
		schedule();
	}
}

void mutex_unlock(mutex_t *lock) {
	__sync_lock_release(lock);
}

#endif