#include "kernel/page.h"
#include <driver/framebuffer/fb_dm.h>
#include <drivers/vesa_display.h>
#include <drivers/video.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/platform.h>
#include <stdint.h>
#include <string.h>

extern Driver		   core_driver;
struct VesaDisplayInfo vesa_display_info;

DriverResult vesa_display_device_init(void *device);
DriverResult vesa_display_device_start(void *device);

DeviceOps vesa_display_device_ops = {
	.init	 = vesa_display_device_init,
	.start	 = vesa_display_device_start,
	.stop	 = NULL,
	.destroy = NULL,
};

DeviceDriver vesa_display_device_driver;

DriverResult register_vesa_display(void) {
	FrameBufferDevice *fb_device;
	register_device_driver(&core_driver, &vesa_display_device_driver);

	DriverResult result = create_framebuffer_device(
		&fb_device, &vesa_display_device_ops, platform_device,
		&vesa_display_device_driver);
	if (result != DRIVER_OK) { return result; }

	return DRIVER_OK;
}

#define SEG_ADDR2LINEAR_ADDR(addr)                              \
	(get_vaddr_base() +                                         \
	 (unsigned int *)(((unsigned int)(addr) >> 12) & 0xffff0) + \
	 ((unsigned int)(addr) & 0xffff))

DriverResult vesa_display_device_init(void *device) {
	vesa_display_info.vbe_mode_info.OemStringPtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info.OemStringPtr);
	vesa_display_info.vbe_mode_info.VideoModePtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info.VideoModePtr);
	vesa_display_info.vbe_mode_info.OemVendorNamePtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info.OemVendorNamePtr);
	vesa_display_info.vbe_mode_info.OemProduceRevPtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info.OemProduceRevPtr);
	vesa_display_info.vbe_mode_info.OemProductNamePtr =
		SEG_ADDR2LINEAR_ADDR(vesa_display_info.vbe_mode_info.OemProductNamePtr);
	return DRIVER_OK;
}

DriverResult vesa_display_device_start(void *device) {
	FrameBufferDevice *fb_device		 = ((LogicalDevice *)device)->dm_ext;
	fb_device->mode_info.width			 = vesa_display_info.width;
	fb_device->mode_info.height			 = vesa_display_info.height;
	fb_device->mode_info.bits_per_pixel	 = vesa_display_info.BitsPerPixel;
	fb_device->mode_info.bytes_per_pixel = vesa_display_info.BitsPerPixel / 8;

	uint32_t vram_size = fb_device->mode_info.width *
						 fb_device->mode_info.height *
						 fb_device->mode_info.bytes_per_pixel;

	fb_device->framebuffer_address = ioremap(
		(size_t)vesa_display_info.vram_phy, vram_size,
		PAGE_CACHE_WRITE_COMBINE);

	return DRIVER_OK;
}
