#include <kernel/softirq.h>
#include <stddef.h>

static SoftirqHandler softirq_handlers[SOFTIRQ_MAX];

DriverResult softirq_register_handler(
	SoftirqType type, SoftirqHandler handler) {
	if (type < 0 || type >= SOFTIRQ_MAX) {
		return DRIVER_ERROR_INVALID_TYPE;
	}
	if (handler == NULL) { return DRIVER_ERROR_NULL_POINTER; }
	if (softirq_handlers[type] != NULL) { return DRIVER_ERROR_CONFLICT; }

	softirq_handlers[type] = handler;
	return DRIVER_OK;
}

void softirq_dispatch(uint8_t pending) {
	for (int type = 0; type < SOFTIRQ_MAX; type++) {
		if ((pending & (1U << type)) != 0 && softirq_handlers[type] != NULL) {
			softirq_handlers[type]();
		}
	}
}
