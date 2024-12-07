#ifndef _VIDEO_DM_H
#define _VIDEO_DM_H

#include "driver/video.h"
#include "kernel/device.h"
#include "kernel/device_driver.h"
#include "kernel/list.h"
#include "stdint.h"

typedef struct VideoModeInfo {
	uint16_t width;
	uint16_t height;
	uint8_t	 bits_per_pixel;
	uint8_t	 bytes_per_pixel;
} VideoModeInfo;

typedef struct VideoDevice {
	list_t video_list_lh;

	Device		 *device;
	VideoModeInfo mode_info;
	uint8_t		 *framebuffer_address;

	FramebufferOps *framebuffer_ops;
} VideoDevice;

typedef struct VideoDeviceManager {
	Device *main_display_device;
} VideoDeviceManager;

extern struct DeviceManager video_device_manager;

DriverResult register_video_device(
	DeviceDriver *device_driver, Device *device, VideoDevice *video_device);
DriverResult unregister_video_devce(
	DeviceDriver *device_driver, Device *device, VideoDevice *video_device);
DriverResult video_get_video_device(int in_index, VideoDevice **out_device);
#endif