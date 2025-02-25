#ifndef _RWLOCK_H
#define _RWLOCK_H

#include "kernel/condvar.h"
#include "kernel/spinlock.h"

typedef struct {
	spinlock_t status_lock; // 保护读写锁内部状态
	condvar_t  read_lock;
	condvar_t  write_lock;
	int		   read_count;
	int		   write_count;
	int		   write_waiting;
} rwlock_t;

void rwlock_init(rwlock_t *lock) {
	spinlock_init(&lock->status_lock);
	condvar_init(&lock->read_lock);
	condvar_init(&lock->write_lock);
	lock->read_count	= 0;
	lock->write_count	= 0;
	lock->write_waiting = 0;
}

void rwlock_read_lock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	while (lock->write_count > 0 || lock->write_waiting > 0) {
		condvar_wait(&lock->read_lock, &lock->status_lock);
	}
	lock->read_count++;
	spin_unlock(&lock->status_lock);
}

void rwlock_read_unlock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	lock->read_count--;
	if (lock->read_count == 0 && lock->write_waiting > 0) {
		condvar_signal(&lock->write_lock);
	}
	spin_unlock(&lock->status_lock);
}

void rwlock_write_lock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	lock->write_waiting++;
	while (lock->read_count > 0 || lock->write_count > 0) {
		condvar_wait(&lock->write_lock, &lock->status_lock);
	}
	lock->write_waiting--;
	lock->write_count++;
	spin_unlock(&lock->status_lock);
}

void rwlock_write_unlock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	lock->write_count--;
	if (lock->write_count > 0) {
		condvar_signal(&lock->write_lock);
	} else {
		condvar_broadcast(&lock->read_lock);
	}
	spin_unlock(&lock->status_lock);
}

#endif