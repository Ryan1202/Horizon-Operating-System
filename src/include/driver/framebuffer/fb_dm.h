#ifndef _FRAMEBUFFER_DM_H
#define _FRAMEBUFFER_DM_H

#include <driver/framebuffer/console_backend.h>
#include <driver/framebuffer/fb.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/list.h>
#include <stdint.h>

typedef struct FrameBufferModeInfo {
	uint16_t width;
	uint16_t height;
	uint8_t	 bits_per_pixel;
	uint8_t	 bytes_per_pixel;
} FrameBufferModeInfo;

typedef struct FrameBufferDevice {
	list_t fb_list_lh;

	LogicalDevice	   *device;
	FrameBufferModeInfo mode_info;
	uint8_t			   *framebuffer_address;

	FramebufferOps *framebuffer_ops;

	FrameBufferConsoleBackend console_backend;
} FrameBufferDevice;

typedef struct FrameBufferDeviceManager {
	LogicalDevice *main_display_device;
	uint8_t		   new_fb_device_num;
	uint8_t		   fb_device_count;
} FrameBufferDeviceManager;

extern struct DeviceManager framebuffer_dm;

DriverResult create_framebuffer_device(
	FrameBufferDevice **fb_device, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver);
DriverResult delete_framebuffer_device(FrameBufferDevice *framebuffer_device);
DriverResult framebuffer_start_all();
DriverResult framebuffer_get_device(
	int in_index, FrameBufferDevice **out_device);
#endif