#include "objects/object.h"
#include <driver/framebuffer/fb_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <result.h>
#include <string.h>

DriverResult framebuffer_device_start(DeviceManager *manager, Device *device);

DeviceManagerOps framebuffer_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = framebuffer_device_start,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

FrameBufferDeviceManager framebuffer_dm_ext;

struct DeviceManager framebuffer_dm = {
	.type = DEVICE_TYPE_FRAMEBUFFER,

	.ops = &framebuffer_dm_ops,

	.private_data = &framebuffer_dm_ext,
};

DriverResult framebuffer_dm_load(DeviceManager *manager) {
	FrameBufferDeviceManager *framebuffer_manager = manager->private_data;
	framebuffer_manager->main_display_device	  = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult framebuffer_dm_unload(DeviceManager *manager) {
	FrameBufferDeviceManager *framebuffer_manager = manager->private_data;
	framebuffer_manager->main_display_device	  = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult register_framebuffer_device(
	DeviceDriver *device_driver, Device *device,
	FrameBufferDevice *framebuffer_device, ObjectAttr *attr) {
	device->dm_ext = framebuffer_device;
	if (device->dm_ext == NULL) { return DRIVER_RESULT_OUT_OF_MEMORY; }
	framebuffer_device->device = device;

	string_t name;
	string_new_with_number(
		&name, "FrameBuffer", 5, framebuffer_dm_ext.fb_device_count++);

	DRV_RESULT_DELIVER_CALL(
		register_device, device_driver, &name, device->bus, device, attr);

	list_init(&framebuffer_device->fb_list_lh);
	list_add_tail(&device->dm_list, &framebuffer_dm.device_lh);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_framebuffer_devce(
	DeviceDriver *device_driver, Device *device,
	FrameBufferDevice *framebuffer_device) {
	list_del(&framebuffer_device->fb_list_lh);
	return DRIVER_RESULT_OK;
}

DriverResult framebuffer_device_start(DeviceManager *manager, Device *device) {
	FrameBufferDevice *framebuffer_device = (FrameBufferDevice *)device->dm_ext;

	if (framebuffer_dm_ext.main_display_device == NULL) {
		framebuffer_dm_ext.main_display_device = device;
	}
	if (framebuffer_device->framebuffer_address == NULL) {
		return DRIVER_RESULT_OTHER_ERROR;
	}
	if (framebuffer_device->mode_info.bits_per_pixel == 8) {
		framebuffer_device->framebuffer_ops = &fb_ops_8;
	} else if (framebuffer_device->mode_info.bits_per_pixel == 16) {
		framebuffer_device->framebuffer_ops = &fb_ops_16;
	} else if (framebuffer_device->mode_info.bits_per_pixel == 24) {
		framebuffer_device->framebuffer_ops = &fb_ops_24;
	} else if (framebuffer_device->mode_info.bits_per_pixel == 32) {
		framebuffer_device->framebuffer_ops = &fb_ops_32;
	} else {
		return DRIVER_RESULT_UNSUPPORT_FEATURE;
	}

	return DRIVER_RESULT_OK;
}

DriverResult framebuffer_get_device(
	int in_index, FrameBufferDevice **out_device) {
	Device *device;
	if (in_index == 0) {
		*out_device = framebuffer_dm_ext.main_display_device->dm_ext;
		return DRIVER_RESULT_OK;
	}
	int i = 0;
	list_for_each_owner (device, &framebuffer_dm.device_lh, dm_list) {
		if (device == framebuffer_dm_ext.main_display_device) { continue; }
		if (i == in_index) {
			*out_device = device->dm_ext;
			return DRIVER_RESULT_OK;
		}
		i++;
	}
	return DRIVER_RESULT_NOT_EXIST;
}
