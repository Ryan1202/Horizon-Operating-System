#include "types.h"
#include <kernel/rwlock.h>

static int rwlock_try_read(void *context) {
	rwlock_t *lock = context;
	if (lock->write_count != 0 || lock->write_waiting != 0) return false;
	lock->read_count++;
	return true;
}

static int rwlock_try_write(void *context) {
	rwlock_t *lock = context;
	if (lock->read_count != 0 || lock->write_count != 0) return false;
	lock->write_waiting--;
	lock->write_count++;
	return true;
}

DriverResult rwlock_init(rwlock_t *lock) {
	spinlock_init(&lock->status_lock);
	lock->readers = wait_queue_create();
	if (lock->readers == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;

	lock->writers = wait_queue_create();
	if (lock->writers == NULL) {
		wait_queue_destroy(lock->readers);
		lock->readers = NULL;
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}

	lock->read_count    = 0;
	lock->write_count   = 0;
	lock->write_waiting = 0;
	return DRIVER_OK;
}

void rwlock_destroy(rwlock_t *lock) {
	wait_queue_destroy(lock->readers);
	wait_queue_destroy(lock->writers);
	lock->readers = NULL;
	lock->writers = NULL;
}

void rwlock_read_lock(rwlock_t *lock) {
	wait_queue_wait(
		lock->readers, &lock->status_lock, rwlock_try_read, lock);
}

void rwlock_read_unlock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	lock->read_count--;
	if (lock->read_count == 0 && lock->write_waiting > 0) {
		wait_queue_wake_one(lock->writers);
	}
	spin_unlock(&lock->status_lock);
}

bool rwlock_write_try_lock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	if (lock->read_count == 0 && lock->write_count == 0) {
		lock->write_count++;
		spin_unlock(&lock->status_lock);
		return true;
	}
	spin_unlock(&lock->status_lock);
	return false;
}

void rwlock_write_lock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	lock->write_waiting++;
	spin_unlock(&lock->status_lock);

	wait_queue_wait(
		lock->writers, &lock->status_lock, rwlock_try_write, lock);
}

void rwlock_write_unlock(rwlock_t *lock) {
	spin_lock(&lock->status_lock);
	lock->write_count--;
	if (lock->write_waiting > 0) {
		wait_queue_wake_one(lock->writers);
	} else {
		wait_queue_wake_all(lock->readers);
	}
	spin_unlock(&lock->status_lock);
}
