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

void rwlock_init(rwlock_t *lock);
void rwlock_read_lock(rwlock_t *lock);
void rwlock_read_unlock(rwlock_t *lock);
bool rwlock_write_try_lock(rwlock_t *lock);
void rwlock_write_lock(rwlock_t *lock);
void rwlock_write_unlock(rwlock_t *lock);

#endif