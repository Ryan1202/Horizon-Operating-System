#include <driver/bus_dm.h>
#include <driver/framebuffer/fb_dm.h>
#include <driver/input/input_dm.h>
#include <driver/interrupt/interrupt_dm.h>
#include <driver/network/network_dm.h>
#include <driver/serial/serial_dm.h>
#include <driver/sound/sound_dm.h>
#include <driver/storage/disk/volume.h>
#include <driver/storage/storage_dm.h>
#include <driver/time_dm.h>
#include <driver/timer/timer_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <result.h>

DeviceManager *device_managers[DEVICE_TYPE_MAX] = {
	[DEVICE_TYPE_INTERRUPT_CONTROLLER] = &interrupt_dm,
	[DEVICE_TYPE_TIMER]				   = &timer_dm,
	[DEVICE_TYPE_TIME]				   = &time_dm,
	[DEVICE_TYPE_FRAMEBUFFER]		   = &framebuffer_dm,
	[DEVICE_TYPE_STORAGE]			   = &storage_dm,
	[DEVICE_TYPE_INPUT]				   = &input_dm,
	[DEVICE_TYPE_SOUND]				   = &sound_dm,
	[DEVICE_TYPE_ETHERNET]			   = &network_dm,
	[DEVICE_TYPE_SERIAL]			   = &serial_dm,
	[DEVICE_TYPE_BUS_CONTROLLER]	   = &bus_controller_dm,
};

DriverResult init_device_managers() {
	DeviceManager *device_manager;
	for (int i = 0; i < DEVICE_TYPE_MAX; i++) {
		device_manager = device_managers[i];
		if (device_manager == NULL) continue;
		list_init(&device_manager->device_lh);
		if (device_manager->ops->dm_load != NULL)
			DRIVER_RESULT_PASS(device_manager->ops->dm_load(device_manager));
	}
	return DRIVER_OK;
}
