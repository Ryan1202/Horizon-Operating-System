#ifndef _SOFTIRQ_H
#define _SOFTIRQ_H

#include <kernel/driver.h>
#include <stdint.h>

#define pending_softirq() ({ softirq.pending = 1; })

typedef enum SoftirqType {
	SOFTIRQ_TIMER,
	SOFTIRQ_NETWORK,
	SOFTIRQ_BLOCK,
	SOFTIRQ_SCHEDULER,
	SOFTIRQ_MAX
} SoftirqType;

typedef struct Softirq {
	uint8_t pending : 1;
} Softirq;

typedef struct SoftirqHandler {
	void (*handler)();
} SoftirqHandler;

extern Softirq softirq;

void		 do_softirq(void);
DriverResult softirq_register_handler(SoftirqType type, void (*handler)(void));

#endif