#include "kernel/device.h"
#include "kernel/list.h"
#include "objects/object.h"
#include <driver/sound/sound_dm.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/memory.h>

DriverResult sound_dm_load(DeviceManager *manager);
DriverResult sound_dm_unload(DeviceManager *manager);

DeviceManagerOps sound_dm_ops = {
	.dm_load   = sound_dm_load,
	.dm_unload = sound_dm_unload,
};

SoundDeviceManager sound_dm_ext;
DeviceManager	   sound_dm = {
		 .type		   = DEVICE_TYPE_SOUND,
		 .ops		   = &sound_dm_ops,
		 .private_data = &sound_dm_ext,
};

DriverResult sound_dm_load(DeviceManager *manager) {
	manager->private_data = kmalloc(sizeof(SoundDeviceManager));
	return DRIVER_RESULT_OK;
}

DriverResult sound_dm_unload(DeviceManager *manager) {
	kfree(manager->private_data);
	return DRIVER_RESULT_OK;
}

DriverResult register_sound_device(
	DeviceDriver *driver, Device *device, SoundDevice *sound_device,
	ObjectAttr *attr) {
	device->dm_ext			   = sound_device;
	sound_device->device	   = device;
	sound_device->private_data = device->private_data;

	list_add_tail(&device->dm_list, &sound_dm.device_lh);

	string_t name;
	string_new_with_number(&name, "Sound", 5, sound_dm_ext.device_count++);
	DRIVER_RESULT_PASS(register_device(
		device->device_driver, &name, device->bus, device, attr));

	return DRIVER_RESULT_OK;
}

SoundDevice *sound_get_device(Object *object) {
	if (object->attr->type != OBJECT_TYPE_DEVICE) return NULL;
	Device *device = object->value.device;
	if (device->device_driver->type != DEVICE_TYPE_SOUND) return NULL;
	return device->dm_ext;
}

SoundDeviceType sound_get_type(Object *object) {
	SoundDevice *sound_device = sound_get_device(object);
	if (sound_device == NULL) return SOUND_TYPE_UNKNOWN;
	return sound_device->type;
}
