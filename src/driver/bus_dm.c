#include <driver/bus_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/memory.h>

DeviceManagerOps bus_controller_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = NULL,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

DeviceManager bus_controller_dm = {
	.type		  = DEVICE_TYPE_BUS_CONTROLLER,
	.ops		  = &bus_controller_dm_ops,
	.private_data = NULL,
};
