#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include "kernel/driver_interface.h"
extern struct task_s *current_task;

#ifndef DEBUG
typedef volatile int spinlock_t;

#define SPINLOCK(lock) spinlock_t lock = 0;

#define SPINLOCK_INIT(lock) \
	{ (lock) = 0; }

static inline void spinlock_init(spinlock_t *lock) {
	*lock = 0;
}

#define SPINLOCK_GET(lock) *lock
#else
typedef volatile struct {
	int			   lock;
	struct task_s *owner;
} spinlock_t;

#define SPINLOCK(lock) spinlock_t lock = {0, NULL};

#define SPINLOCK_INIT(lock) \
	{ (lock) = {0, NULL}; }

#define SPINLOCK_GET(lock) lock->lock
static inline void spinlock_init(spinlock_t *lock) {
	lock->lock	= 0;
	lock->owner = NULL;
}
#endif

static inline void spin_lock(spinlock_t *lock) {
	// 使用__atomic内建函数实现原子操作，并显式指定内存序
	int expected;
	for (;;) {
		expected = 0;
		if (__atomic_compare_exchange_n(
				&SPINLOCK_GET(lock), &expected, 1, 0, __ATOMIC_ACQUIRE,
				__ATOMIC_RELAXED)) {
			break;
		}
		while (__atomic_load_n(&SPINLOCK_GET(lock), __ATOMIC_RELAXED))
			;
	}
#ifdef DEBUG
	lock->owner = current_task;
#endif
}

static inline int spin_try_lock(spinlock_t *lock) {
	int expected = 0;
	int ret		 = __atomic_compare_exchange_n(
		 &SPINLOCK_GET(lock), &expected, 1, 0, __ATOMIC_ACQUIRE,
		 __ATOMIC_RELAXED);
#ifdef DEBUG
	if (ret) lock->owner = current_task;
#endif
	return ret;
}

static inline void spin_unlock(volatile spinlock_t *lock) {
	__atomic_store_n(&SPINLOCK_GET(lock), 0, __ATOMIC_RELEASE);
#ifdef DEBUG
	lock->owner = NULL;
#endif
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