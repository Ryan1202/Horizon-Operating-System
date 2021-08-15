#ifndef _SPINLOCK_H
#define _SPINLOCK_H

typedef int spinlock_t;

static inline void spinlock_init(spinlock_t *lock)
{
	lock = 0;
}

static inline void spin_lock(spinlock_t *lock)
{
	//使用gcc提供的__sync_bool_compare_and_swap()实现原子操作
	while (!__sync_bool_compare_and_swap(lock, 0, 1))
	{
		while (*lock);
	}
}

static inline void spin_unlock(spinlock_t __volatile__ *lock)
{
	__asm__ __volatile__("":::"memory");
	*lock = 0;
}

#endif