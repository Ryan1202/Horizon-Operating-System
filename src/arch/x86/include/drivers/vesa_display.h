#ifndef _VESA_DISPLAY_H
#define _VESA_DISPLAY_H

struct VbeModeInfoBlock {
	unsigned char  VbeSignature[4];
	unsigned short VbeVersion;
	unsigned int  *OemStringPtr;
	unsigned int   Capabilities;
	unsigned int  *VideoModePtr;
	unsigned short TotalMemory;
	unsigned short OemSoftwareRev;
	unsigned int  *OemVendorNamePtr;
	unsigned int  *OemProductNamePtr;
	unsigned int  *OemProduceRevPtr;
	unsigned char  Reserved[222];
	unsigned char  OemData;
} __attribute__((packed));

struct VbeControlInfoBlock {
	unsigned char  VbeSignature[4];
	unsigned short VbeVersion;
	unsigned int   OemStringPtr;
	unsigned char  Capabilities[4];
	unsigned int   VideoModePtr;
	unsigned short TotalMemory;
	unsigned short OemSoftwareRev;
	unsigned int   OemVendorNamePtr;
	unsigned int   OemProductNamePtr;
	unsigned int   OemProduceRevPtr;
	unsigned char  OemData[256];
};

struct VesaDisplayInfo {
	unsigned short				width, height;
	unsigned short				BitsPerPixel;
	unsigned char			   *vram;
	struct VbeModeInfoBlock	   *vbe_mode_info;
	struct VbeControlInfoBlock *vbe_conrtol_info;
};

extern struct Device vesa_display_device;
void				 register_vesa_display(void);

#endif