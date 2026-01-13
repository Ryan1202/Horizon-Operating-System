#include "kernel/softirq.h"
#include "kernel/driver.h"
#include "kernel/thread.h"

Softirq softirq = {0};

SoftirqHandler softirq_handlers[SOFTIRQ_MAX];

void do_softirq(void) {
	if (softirq.pending && !in_softirq()) {
		disable_preempt();
		softirq_enter();

		for (int i = 0; i < SOFTIRQ_MAX; i++) {
			if (softirq_handlers[i].handler != NULL) {
				softirq_handlers[i].handler();
			}
		}

		softirq.pending = 0;
		softirq_exit();
		enable_preempt();
	}
}

DriverResult softirq_register_handler(SoftirqType type, void (*handler)(void)) {
	if (type == SOFTIRQ_MAX) { return DRIVER_ERROR_INVALID_TYPE; }
	if (softirq_handlers[type].handler != NULL) {
		return DRIVER_ERROR_CONFLICT;
	}

	softirq_handlers[type].handler = handler;

	return DRIVER_OK;
}
