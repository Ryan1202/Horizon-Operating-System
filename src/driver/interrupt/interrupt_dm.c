#include <driver/interrupt/interrupt_dm.h>

static DeviceManagerOps interrupt_dm_ops = {0};

struct DeviceManager interrupt_dm = {
    .type = DEVICE_TYPE_INTERRUPT_CONTROLLER,
    .ops = &interrupt_dm_ops,
    .private_data = NULL,
};
