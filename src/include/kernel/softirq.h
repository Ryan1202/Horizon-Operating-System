#ifndef _SOFTIRQ_H
#define _SOFTIRQ_H

#include <kernel/driver.h>
#include <stdint.h>

typedef enum SoftirqType : uint8_t {
	SOFTIRQ_TIMER,
	SOFTIRQ_USB,
	SOFTIRQ_NETWORK,
	SOFTIRQ_BLOCK,
	SOFTIRQ_SCHEDULER,
	SOFTIRQ_MAX
} SoftirqType;

typedef void (*SoftirqHandler)(void);

DriverResult softirq_register_handler(SoftirqType type, SoftirqHandler handler);
void		 softirq_raise(SoftirqType type);
void		 softirq_dispatch(uint8_t pending);

#endif
