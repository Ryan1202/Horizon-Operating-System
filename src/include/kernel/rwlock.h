#ifndef _RWLOCK_H
#define _RWLOCK_H

#include <kernel/driver.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>

typedef struct {
	spinlock_t status_lock;
	WaitQueue  readers;
	WaitQueue  writers;
	int		   read_count;
	int		   write_count;
	int		   write_waiting;
} rwlock_t;

DriverResult rwlock_init(rwlock_t *lock);
void		 rwlock_read_lock(rwlock_t *lock);
void		 rwlock_read_unlock(rwlock_t *lock);
bool		 rwlock_write_try_lock(rwlock_t *lock);
void		 rwlock_write_lock(rwlock_t *lock);
void		 rwlock_write_unlock(rwlock_t *lock);

#endif
