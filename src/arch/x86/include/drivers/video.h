#ifndef _VIDEO_H
#define _VIDEO_H

#define VIDEO_INFO_ADDR 0x8000

/**
 * 虚拟地址VRAM_VIR_ADDR =
 * |  0000 0000 01  |  00 0000 0000  |  0000 0000 0000  |
 * |  一级页表索引  |  二级页表索引  |      偏移地址    |
 * 对应*(VIDEO_INFO_ADDR + 6)的地址
 */
#define VRAM_VIR_ADDR 0x400000

struct vbe_info_block {
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

struct video_info {
    unsigned short         width, height;
    unsigned short         BitsPerPixel;
    unsigned char         *vram;
    struct vbe_info_block *vbe_info;
};

extern struct video_info VideoInfo;

// 写像素
void write_pixel(int x, int y, unsigned int color);
// 获取显示模式信息
void init_video(void);
// 输出VBE信息
void show_vbeinfo(void);
// 输出字符
void print_word(int x, int y, unsigned char *ascii, unsigned int color);
// 输出字符串
void print_string(int x, int y, unsigned int color, unsigned char *font, char *string);

void draw_rect(int x, int y, int width, int height, int color);

#endif