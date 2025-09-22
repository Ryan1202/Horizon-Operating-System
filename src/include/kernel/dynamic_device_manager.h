#ifndef _DYNAMIC_DM_H
#define _DYNAMIC_DM_H

#include <kernel/device.h>
#include <kernel/device_manager.h>
#include <kernel/list.h>

struct DynamicDevice;
typedef struct DynamicDeviceEvents {
	void (*probe)(DeviceManager *ddm);
	void (*remove)(struct DynamicDevice *device);
} DynamicDeviceEvents;

typedef struct DynamicDevice {
	list_t list;

	Device *device;
} DynamicDevice;

typedef struct DynamicDeviceManager {
	list_t list;
	list_t dynamic_device_lh;

	DeviceManager		*device_manager;
	DynamicDeviceEvents *events;
} DynamicDeviceManager;

extern list_t new_device_lh;

DriverResult register_dynamic_device_manager(
	DynamicDeviceEvents *events, DeviceManager *device_manager);
void dynamic_device_manager(void *arg);

#endif