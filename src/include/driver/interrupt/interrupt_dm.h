#ifndef _INTERRUPT_DM_H
#define _INTERRUPT_DM_H

#include <kernel/device_manager.h>

typedef struct IrqDomain IrqDomain;

extern struct DeviceManager interrupt_dm;

void interrupt_init(void);

#endif
