#include "drivers/video.h"
#include <driver/video_dm.h>
#include <drivers/vesa_display.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/platform.h>
#include <stdint.h>
#include <string.h>

struct VesaDisplayInfo vesa_display_info;

DriverResult vesa_display_device_init(Device *device);
DriverResult vesa_display_device_start(Device *device);

DeviceDriverOps vesa_display_driver_ops = {
	.register_driver_hook	= NULL,
	.unregister_driver_hook = NULL,
};
DeviceOps vesa_display_device_ops = {
	.init	 = vesa_display_device_init,
	.start	 = vesa_display_device_start,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};

Driver vesa_display_driver = {
	.name = STRING_INIT("vesa display driver"),
};
DeviceDriver vesa_display_device_driver = {
	.name	  = STRING_INIT("vesa display device driver"),
	.bus	  = &platform_bus,
	.type	  = DEVICE_TYPE_VIDEO,
	.priority = DRIVER_PRIORITY_BASIC,
	.state	  = DRIVER_STATE_UNREGISTERED,
	.ops	  = &vesa_display_driver_ops,
};
Device vesa_display_device = {
	.name			   = STRING_INIT("vesa display"),
	.device_driver	   = &vesa_display_device_driver,
	.ops			   = &vesa_display_device_ops,
	.private_data_size = 0,
};
VideoDevice vesa_display_video_device = {
	.device = &vesa_display_device,
};

void register_vesa_display(void) {
	register_driver(&vesa_display_driver);
	register_device_driver(&vesa_display_driver, &vesa_display_device_driver);
	register_video_device(
		&vesa_display_device_driver, &vesa_display_device,
		&vesa_display_video_device);
}

#define SEG_ADDR2LINEAR_ADDR(addr)                              \
	((unsigned int *)(((unsigned int)(addr) >> 12) & 0xffff0) + \
	 ((unsigned int)(addr) & 0xffff))

DriverResult vesa_display_device_init(Device *device) {
	vesa_display_info.vbe_mode_info->OemStringPtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info->OemStringPtr);
	vesa_display_info.vbe_mode_info->VideoModePtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info->VideoModePtr);
	vesa_display_info.vbe_mode_info->OemVendorNamePtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info->OemVendorNamePtr);
	vesa_display_info.vbe_mode_info->OemProduceRevPtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info->OemProduceRevPtr);
	vesa_display_info.vbe_mode_info->OemProductNamePtr = SEG_ADDR2LINEAR_ADDR(
		vesa_display_info.vbe_mode_info->OemProductNamePtr);
	return DRIVER_RESULT_OK;
}

DriverResult vesa_display_device_start(Device *device) {
	vesa_display_info.vram				   = (uint8_t *)VRAM_VIR_ADDR;
	VideoDevice *video_device			   = device->driver_manager_extension;
	video_device->mode_info.width		   = vesa_display_info.width;
	video_device->mode_info.height		   = vesa_display_info.height;
	video_device->mode_info.bits_per_pixel = vesa_display_info.BitsPerPixel;
	video_device->mode_info.bytes_per_pixel =
		vesa_display_info.BitsPerPixel / 8;
	video_device->framebuffer_address = vesa_display_info.vram;

	return DRIVER_RESULT_OK;
}
