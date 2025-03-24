#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include "kernel/driver_interface.h"
extern struct task_s *current_task;
typedef volatile int  spinlock_t;

#define SPINLOCK(lock) spinlock_t lock = 0;

#define SPINLOCK_INIT(lock) \
	{ (lock) = 0; }

static inline void spinlock_init(spinlock_t *lock) {
	lock = 0;
}

static inline void spin_lock(spinlock_t *lock) {
	// 使用gcc提供的__sync_bool_compare_and_swap()实现原子操作
	while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
		while (*lock)
			;
	}
}

static inline int spin_try_lock(spinlock_t *lock) {
	return __sync_bool_compare_and_swap(lock, 0, 1);
}

static inline void spin_unlock(volatile spinlock_t *lock) {
	__asm__ __volatile__("" ::: "memory");
	*lock = 0;
}

// 获取自旋锁的同时禁用中断并保存中断状态
static inline int spin_lock_irqsave(spinlock_t *lock) {
	int flags = save_and_disable_interrupt();
	spin_lock(lock);
	return flags;
}

static inline int spin_try_lock_irqsave(spinlock_t *lock) {
	int flags = save_and_disable_interrupt();
	if (spin_try_lock(lock)) { return flags; }
	store_interrupt_status(flags);
	return 0;
}

// 释放自旋锁并恢复之前保存的中断状态
static inline void spin_unlock_irqrestore(
	spinlock_t *lock, unsigned long flags) {
	spin_unlock(lock);
	store_interrupt_status(flags);
}

#endif