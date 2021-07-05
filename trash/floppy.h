#ifndef _FLOPPY_H
#define _FLOPPY_H

#define FLOPPY_DOR			0x3f2
#define FLOPPY_STATUS		0x3f4
#define FLOPPY_DATA			0x3f5
#define FLOPPY_DIR			0x3f7
#define FLOPPY_CCR			0x3f7

//DOR参数
#define FLOPPY_SEL_A		0x00
#define FLOPPY_SEL_B		0x01
#define FLOPPY_SEL_C		0x02
#define FLOPPY_SEL_D		0x03
#define FLOPPY_RESET		0x04	//0:复位模式 1:正常
#define FLOPPY_DMA_INT		0x08	//0:启用IRQ 1:启用DMA
#define FLOPPY_EN_MOTA		0x10
#define FLOPPY_EN_MOTB		0x20
#define FLOPPY_EN_MOTC		0x40
#define FLOPPY_EN_MOTD		0x80

//FDC状态
#define FLOPPY_BUSY_A		0x01
#define FLOPPY_BUSY_B		0x02
#define FLOPPY_BUSY_C		0x04
#define FLOPPY_BUSY_D		0x08
#define FLOPPY_BUSY_FDC		0x10
#define FLOPPY_IS_DMA		0x20
#define FLOPPY_IO_DIR		0x40	//传输方向(0:CPU-DFC 1:FDC-CPU)
#define FLOPPY_READY		0x80	//FDC数据寄存器准备就绪

//命令
#define FLOPPY_SPECIFY		0x03
#define FLOPPY_WRITE_DATA	0x05
#define FLOPPY_READ_DATA	0x06
#define FLOPPY_RECALIBRATE	0x07
#define FLOPPY_SENCE_INT	0x08
#define FLOPPY_FORMAT_TRACK	0x0d
#define FLOPPY_SEEK			0x0f
#define FLOPPY_VERSION		0x10
#define FLOPPY_CONFIGURE	0x13
#define FLOPPY_LOCK			0x14

#define FLOPPY_DMA			2
#define FLOPPY_DMABUF		0x8c000
#define FLOPPY_IRQ			6

extern struct floppys floppys;

struct floppy_info {
	unsigned int cyl, hed, sec, len;
};
struct floppys
{
	int num;
	struct floppy_info type[4];
};

void init_floppy(void);
int floppy_num(void);
int floppy_check_version(void);
void floppy_getinfo(void);
void send_cmd(char cmd);
int recv_data(void);
void wait_fd_ready(int fdnum);
void floppy_handler(int irq);
void floppy_test_read(void);

#endif