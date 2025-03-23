#ifndef _SOUND_DM_H
#define _SOUND_DM_H

#include "kernel/device.h"
#include "kernel/device_driver.h"
#include "kernel/device_manager.h"
#include "kernel/driver.h"
#include <stdint.h>

#define SOUND_DEVICE_MODE_PLAY	  0
#define SOUND_DEVICE_MODE_RECORED 1

struct SoundDevice;
typedef struct SoundDeviceOps {
	DriverResult (*set_volume)(struct SoundDevice *device, int volume);
} SoundDeviceOps;

typedef struct SoundDeviceCapabilities {
	uint32_t record			 : 1;
	uint32_t play			 : 1;
	uint32_t set_volume		 : 1;
	uint32_t set_sample_rate : 1;
} SoundDeviceCapabilities;

typedef enum SoundDeviceType {
	SOUND_TYPE_UNKNOWN,
	SOUND_TYPE_PCM,
} SoundDeviceType;

typedef struct SoundDevice {
	Device				   *device;
	SoundDeviceCapabilities capabilities;
	SoundDeviceOps		   *ops;

	void *private_data;

	SoundDeviceType type;

	union {
		struct PcmDevice *pcm;
	};
} SoundDevice;

typedef struct SoundDeviceManager {
	int device_count;
} SoundDeviceManager;

extern DeviceManager sound_dm;

DriverResult register_sound_device(
	DeviceDriver *driver, Device *device, SoundDevice *sound_device,
	ObjectAttr *attr);

SoundDevice *sound_get_device(Object *object);

#endif