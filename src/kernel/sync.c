/**
 * @file sync.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 参考《操作系统真象还原》的实现
 * @version 0.1
 * @date 2021-07
 */
#include "kernel/driver_interface.h"
#include "kernel/thread.h"
#include "kernel/wait_queue.h"
#include <kernel/console.h>
#include <kernel/func.h>
#include <kernel/sync.h>
#include <stdint.h>

void sema_init(struct semaphore *psema, uint8_t value) {
	psema->value = value;
	wait_queue_init(&psema->wq);
}

void lock_init(struct lock *plock) {
	plock->holder			= NULL;
	plock->holder_repeat_nr = 0;
	sema_init(&plock->semaphore, 1);
}

void sema_down(struct semaphore *psema) {
	int flags = save_and_disable_interrupt();
	while (psema->value == 0) {
		store_interrupt_status(flags);
		thread_set_status(TASK_INTERRUPTIBLE);
		wait_queue_add(&psema->wq);
		thread_wait();
		flags = save_and_disable_interrupt();
	}
	psema->value--;
	store_interrupt_status(flags);
}

void sema_up(struct semaphore *psema) {
	int flags = save_and_disable_interrupt();
	wait_queue_wakeup(&psema->wq);
	psema->value++;
	store_interrupt_status(flags);
}

void lock_acquire(struct lock *plock) {
	if (plock->holder != get_current_thread()) {
		sema_down(&plock->semaphore);
		plock->holder			= get_current_thread();
		plock->holder_repeat_nr = 1;
	} else {
		plock->holder_repeat_nr++;
	}
}

void lock_release(struct lock *plock) {
	if (plock->holder_repeat_nr > 1) {
		plock->holder_repeat_nr--;
		return;
	}
	plock->holder			= NULL;
	plock->holder_repeat_nr = 0;
	sema_up(&plock->semaphore);
}