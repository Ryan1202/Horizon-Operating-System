#include <driver/framebuffer/console_backend.h>
#include <driver/framebuffer/fb_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <result.h>
#include <string.h>

DriverResult framebuffer_device_start(
	DeviceManager *manager, LogicalDevice *device);

DeviceManagerOps framebuffer_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = NULL,
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
	framebuffer_dm_ext.new_fb_device_num		  = 0;
	framebuffer_manager->fb_device_count		  = 0;

	return DRIVER_OK;
}

DriverResult framebuffer_dm_unload(DeviceManager *manager) {
	FrameBufferDeviceManager *framebuffer_manager = manager->private_data;
	framebuffer_manager->main_display_device	  = NULL;

	return DRIVER_OK;
}

DriverResult create_framebuffer_device(
	FrameBufferDevice **fb_device, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_FRAMEBUFFER);
	if (result != DRIVER_OK) return result;

	*fb_device = kzalloc(sizeof(FrameBufferDevice));
	if (*fb_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	FrameBufferDevice *fb  = *fb_device;
	logical_device->dm_ext = fb;
	fb->device			   = logical_device;

	string_t name;
	char	 _name[] = "FrameBuffer";
	string_new_with_number(
		&name, _name, sizeof(_name) - 1, framebuffer_dm_ext.new_fb_device_num);
	framebuffer_dm_ext.new_fb_device_num++;
	framebuffer_dm_ext.fb_device_count++;

	Object *obj = create_object(&device_object, &name, device_object_attr);
	if (obj == NULL) {
		kfree(fb);
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OBJECT;
	}
	obj->value.device.kind	  = DEVICE_KIND_LOGICAL;
	obj->value.device.logical = logical_device;
	logical_device->object	  = obj;

	list_init(&fb->fb_list_lh);

	ConsoleBackend *backend = &fb->console_backend.backend;
	backend->init			= fb_console_backend_init;
	backend->put_string		= fb_console_backend_put_string;

	return DRIVER_OK;
}

DriverResult delete_framebuffer_device(FrameBufferDevice *framebuffer_device) {
	framebuffer_dm_ext.fb_device_count--;

	LogicalDevice *logical_device = framebuffer_device->device;

	list_del(&framebuffer_device->fb_list_lh);
	delete_logical_device(logical_device);
	int result = kfree(framebuffer_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

DriverResult framebuffer_device_start(
	DeviceManager *manager, LogicalDevice *device) {
	FrameBufferDevice *fb_device = (FrameBufferDevice *)device->dm_ext;

	init_and_start_logical_device(device);

	if (framebuffer_dm_ext.main_display_device == NULL) {
		framebuffer_dm_ext.main_display_device = device;
	}
	if (fb_device->framebuffer_address == NULL) { return DRIVER_ERROR_OTHER; }
	if (fb_device->mode_info.bits_per_pixel == 8) {
		fb_device->framebuffer_ops = &fb_ops_8;
	} else if (fb_device->mode_info.bits_per_pixel == 16) {
		fb_device->framebuffer_ops = &fb_ops_16;
	} else if (fb_device->mode_info.bits_per_pixel == 24) {
		fb_device->framebuffer_ops = &fb_ops_24;
	} else if (fb_device->mode_info.bits_per_pixel == 32) {
		fb_device->framebuffer_ops = &fb_ops_32;
	} else {
		return DRIVER_ERROR_UNSUPPORT_FEATURE;
	}
	console_register_backend(&fb_device->console_backend.backend, fb_device);

	return DRIVER_OK;
}

DriverResult framebuffer_start_all() {
	LogicalDevice *device;
	list_for_each_owner (device, &framebuffer_dm.device_lh, dm_device_list) {
		framebuffer_device_start(&framebuffer_dm, device);
	}
	return DRIVER_OK;
}

DriverResult framebuffer_get_device(
	int in_index, FrameBufferDevice **out_device) {
	LogicalDevice *device;
	if (in_index == 0) {
		*out_device = framebuffer_dm_ext.main_display_device->dm_ext;
		return DRIVER_OK;
	}
	int i = 0;
	list_for_each_owner (device, &framebuffer_dm.device_lh, dm_device_list) {
		if (device == framebuffer_dm_ext.main_display_device) { continue; }
		if (i == in_index) {
			*out_device = device->dm_ext;
			return DRIVER_OK;
		}
		i++;
	}
	return DRIVER_ERROR_NOT_EXIST;
}
