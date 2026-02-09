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
	manager->private_data		= kzalloc(sizeof(SoundDeviceManager));
	sound_dm_ext.new_device_num = 0;
	sound_dm_ext.device_count	= 0;
	return DRIVER_OK;
}

DriverResult sound_dm_unload(DeviceManager *manager) {
	kfree(manager->private_data);
	return DRIVER_OK;
}

DriverResult create_sound_device(
	SoundDevice **sound_device, SoundDeviceType type,
	SoundDeviceCapabilities caps, SoundOps *sound_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_SOUND);
	if (result != DRIVER_OK) return result;

	*sound_device = kzalloc(sizeof(SoundDevice));
	if (*sound_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	SoundDevice *snd;
	logical_device->dm_ext = *sound_device;
	snd					   = *sound_device;
	snd->device			   = logical_device;
	snd->type			   = type;
	snd->ops			   = sound_ops;
	snd->capabilities	   = caps;

	char	 _name[] = "Sound";
	string_t name;
	string_new_with_number(
		&name, _name, sizeof(_name) - 1, sound_dm_ext.new_device_num);
	sound_dm_ext.new_device_num++;
	sound_dm_ext.device_count++;

	Object *obj = create_object(&device_object, &name, device_object_attr);
	if (obj == NULL) {
		kfree(snd);
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OBJECT;
	}
	obj->value.device.kind	  = DEVICE_KIND_LOGICAL;
	obj->value.device.logical = logical_device;
	logical_device->object	  = obj;

	return DRIVER_OK;
}

DriverResult delete_sound_device(SoundDevice *sound_device) {
	sound_dm_ext.device_count--;
	LogicalDevice *logical_device = sound_device->device;

	list_del(&logical_device->dm_device_list);
	delete_logical_device(logical_device);
	int result = kfree(sound_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

SoundDevice *sound_get_device(Object *object) {
	// if (object->attr->type != OBJECT_TYPE_DEVICE) return NULL;
	LogicalDevice *device = object->value.device.logical;
	// if (device->type != DEVICE_TYPE_SOUND) return NULL;
	return device->dm_ext;
}

SoundDeviceType sound_get_type(Object *object) {
	SoundDevice *sound_device = sound_get_device(object);
	if (sound_device == NULL) return SOUND_TYPE_UNKNOWN;
	return sound_device->type;
}
