#include <driver/video_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <result.h>

DriverResult video_device_start(DeviceManager *manager, Device *device);

DeviceManagerOps video_dm_ops = {
	.dm_load_hook	= NULL,
	.dm_unload_hook = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = video_device_start,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

VideoDeviceManager video_dm_ext;

struct DeviceManager video_device_manager = {
	.type = DEVICE_TYPE_VIDEO,

	.ops = &video_dm_ops,

	.private_data = &video_dm_ext,
};

DriverResult video_dm_load(DeviceManager *manager) {
	VideoDeviceManager *video_manager  = manager->private_data;
	video_manager->main_display_device = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult video_dm_unload(DeviceManager *manager) {
	VideoDeviceManager *video_manager  = manager->private_data;
	video_manager->main_display_device = NULL;

	return DRIVER_RESULT_OK;
}

DriverResult register_video_device(
	DeviceDriver *device_driver, Device *device, VideoDevice *video_device) {
	device->driver_manager_extension = video_device;
	if (device->driver_manager_extension == NULL) {
		return DRIVER_RESULT_OUT_OF_MEMORY;
	}
	video_device->device = device;

	DRV_RESULT_DELIVER_CALL(register_device, device_driver, device);
	list_init(&video_device->video_list_lh);
	list_add_tail(&device->dm_list, &video_device_manager.device_driver_lh);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_video_devce(
	DeviceDriver *device_driver, Device *device, VideoDevice *video_device) {
	list_del(&video_device->video_list_lh);
	return DRIVER_RESULT_OK;
}

DriverResult video_device_start(DeviceManager *manager, Device *device) {
	VideoDevice *video_device = (VideoDevice *)device->driver_manager_extension;

	if (video_dm_ext.main_display_device == NULL) {
		video_dm_ext.main_display_device = device;
	}
	if (video_device->framebuffer_address == NULL) {
		return DRIVER_RESULT_OTHER_ERROR;
	}
	if (video_device->mode_info.bits_per_pixel == 8) {
		video_device->framebuffer_ops = &fb_ops_8;
	} else if (video_device->mode_info.bits_per_pixel == 16) {
		video_device->framebuffer_ops = &fb_ops_16;
	} else if (video_device->mode_info.bits_per_pixel == 24) {
		video_device->framebuffer_ops = &fb_ops_24;
	} else if (video_device->mode_info.bits_per_pixel == 32) {
		video_device->framebuffer_ops = &fb_ops_32;
	} else {
		return DRIVER_RESULT_UNSUPPORT_DEVICE;
	}

	return DRIVER_RESULT_OK;
}

DriverResult video_get_video_device(int in_index, VideoDevice **out_device) {
	Device *device;
	if (in_index == 0) {
		*out_device =
			video_dm_ext.main_display_device->driver_manager_extension;
		return DRIVER_RESULT_OK;
	}
	int i = 0;
	list_for_each_owner (
		device, &video_device_manager.device_driver_lh, dm_list) {
		if (device == video_dm_ext.main_display_device) { continue; }
		if (i == in_index) {
			*out_device = device->driver_manager_extension;
			return DRIVER_RESULT_OK;
		}
		i++;
	}
	return DRIVER_RESULT_DEVICE_NOT_EXIST;
}
