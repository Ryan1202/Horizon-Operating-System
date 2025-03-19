#include "kernel/thread.h"
#include <kernel/condvar.h>

void condvar_init(condvar_t *cv) {
	wait_queue_init(&cv->wait_queue);
}

void condvar_wait(condvar_t *cv, spinlock_t *mutex) {
	wait_queue_add(&cv->wait_queue);
	thread_set_status(TASK_INTERRUPTIBLE);
	spin_unlock(mutex);

	thread_wait();

	spin_lock(mutex);
}

void condvar_signal(condvar_t *cv) {
	wait_queue_wakeup(&cv->wait_queue);
}

void condvar_broadcast(condvar_t *cv) {
	wait_queue_wakeup_all(&cv->wait_queue);
}