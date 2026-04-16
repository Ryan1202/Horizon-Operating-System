#ifndef _VESA_DISPLAY_H
#define _VESA_DISPLAY_H

#include <stdint.h>
#define VESA_INFO_ADDR 0x2500

#include "kernel/driver.h"
struct VbeModeInfoBlock {
	unsigned char  VbeSignature[4];
	unsigned short VbeVersion;
	size_t		  *OemStringPtr;
	size_t		   Capabilities;
	size_t		  *VideoModePtr;
	unsigned short TotalMemory;
	unsigned short OemSoftwareRev;
	size_t		  *OemVendorNamePtr;
	size_t		  *OemProductNamePtr;
	size_t		  *OemProduceRevPtr;
	unsigned char  Reserved[222];
	unsigned char  OemData;
} __attribute__((packed));

struct VbeControlInfoBlock {
	unsigned char  VbeSignature[4];
	unsigned short VbeVersion;
	size_t		   OemStringPtr;
	unsigned char  Capabilities[4];
	size_t		   VideoModePtr;
	unsigned short TotalMemory;
	unsigned short OemSoftwareRev;
	size_t		   OemVendorNamePtr;
	size_t		   OemProductNamePtr;
	size_t		   OemProduceRevPtr;
	unsigned char  OemData[256];
};

struct VesaDisplayInfo {
	unsigned short			   width, height;
	unsigned short			   BitsPerPixel;
	unsigned char			  *vram_phy;
	struct VbeModeInfoBlock	   vbe_mode_info;
	struct VbeControlInfoBlock vbe_control_info;
};

extern struct LogicalDevice vesa_display_device;

DriverResult register_vesa_display(void);

#endif