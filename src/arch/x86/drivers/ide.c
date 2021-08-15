#include <drivers/pci.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <kernel/descriptor.h>
#include <kernel/fifo.h>
#include <fs/fs.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/driver.h>
#include <stdint.h>
#include <config.h>
#include <types.h>
#include <math.h>

#define ATA_PRIMARY_PORT		0x1f0
#define ATA_SECONDARY_PORT		0x170

#define	ATA_STATUS_BUSY			0x80	//驱动器忙
#define	ATA_STATUS_READY		0x40	//驱动器就绪
#define	ATA_STATUS_DF			0x20	//驱动器写故障
#define	ATA_STATUS_SEEK			0x10	//驱动器查找完成
#define	ATA_STATUS_DRQ			0x08	//请求数据
#define	ATA_STATUS_CORR			0x04	//Correct data
#define	ATA_STATUS_INDEX		0x02	//索引
#define	ATA_STATUS_ERR			0x01	//错误

#define ATA_ERROR_BBK			0x80    //错误块
#define ATA_ERROR_UNC			0x40    //无法矫正的数据
#define ATA_ERROR_MC			0x20    //媒体更改
#define ATA_ERROR_IDNF			0x10    //找不到ID标记
#define ATA_ERROR_MCR			0x08    //媒体更改请求
#define ATA_ERROR_ABRT			0x04    //命令终止
#define ATA_ERROR_TK0NF			0x02    //找不到磁道0
#define ATA_ERROR_AMNF			0x01    //没有地址标记

#define	ATA_CMD_RESTORE			0x10
#define ATA_CMD_READ_PIO		0x20
#define ATA_CMD_READ_PIO_EXT	0x24
#define ATA_CMD_READ_DMA		0xc8
#define ATA_CMD_READ_DMA_EXT	0x25
#define ATA_CMD_WRITE_PIO		0x30
#define ATA_CMD_WRITE_PIO_EXT	0x34
#define ATA_CMD_WRITE_DMA		0xca
#define ATA_CMD_WRITE_DMA_EXT	0x35
#define ATA_CMD_CACHE_FLUSH		0xe7
#define ATA_CMD_CACHE_FLUSH_EXT	0xea
#define ATA_CMD_PACKET			0xa0
#define ATA_CMD_IDENTIFY_PACKET	0xa1
#define ATA_CMD_IDENTIFY		0xec
#define ATAPI_CMD_READ			0xA8
#define ATAPI_CMD_EJECT			0x1B

#define IDE_ATA					0x00
#define IDE_ATAPI				0x01
 
#define ATA_PRIMARY				0x00
#define ATA_SECONDARY			0x01

#define IDE_READ				0x01
#define IDE_WRITE				0x02

#define ATA_MASTER				0x00
#define ATA_SLAVE				0x01

#define ATA_REG_DATA(channel) 			(channel->base + 0)
#define ATA_REG_FEATURE(channel) 		(channel->base + 1)
#define ATA_REG_ERROR(channel) 			(channel->base + 1)
#define ATA_REG_SECTOR_CNT(channel) 	(channel->base + 2)
#define ATA_REG_SECTOR_LOW(channel) 	(channel->base + 3)
#define ATA_REG_SECTOR_MID(channel) 	(channel->base + 4)
#define ATA_REG_SECTOR_HIGH(channel) 	(channel->base + 5)
#define ATA_REG_DEVICE(channel) 		(channel->base + 6)
#define ATA_REG_STATUS(channel) 		(channel->base + 7)
#define ATA_REG_CMD(channel) 			(channel->base + 7)
#define ATA_REG_ALT_STATUS(channel) 	(channel->base + 0x206)
#define ATA_REG_CTL(channel) 			(channel->base + 0x206)

#define IDE_DISK_CNT			0x475	//BIOS数据区中此处保存了磁盘数

#define SECTOR_SIZE				512

#define IDE0_IRQ				14
#define IDE1_IRQ				15

#define IDE_SEND_CMD(channel, cmd) io_out8(ATA_REG_CMD(channel), cmd)

struct ide_identify_info
{
	unsigned short general_config;
	unsigned short obsolete0;
	unsigned short specific_config;
	unsigned short obsolete1;
	unsigned short retired0[2];
	unsigned short obsolete2;
	unsigned short compact_flash[2];
	unsigned short retired1;
	unsigned short serial_number[10];
	unsigned short retired2[2];
	unsigned short obsolete3;
	unsigned short firmware_version[4];
	unsigned short model_number[20];
	unsigned short max_logical_transferred_per_DRQ;
	unsigned short trusted_computing_feature_set_options;
	unsigned short capabilities0;
	unsigned short capabilities1;
	unsigned short obsolete4[2];
	unsigned short report_88_70to_64_valid;
	unsigned short obsolete5[5];
	unsigned short mul_sesc_setting_valid;
	unsigned short lba28sectors[2];
	unsigned short obsolete6;
	unsigned short multword_DMA_select;
	unsigned short PIO_mode_supported;
	unsigned short min_mulword_DMA_cycle_time_per_word;
	unsigned short manufacture_recommend_multiword_DMA_cycle_time;
	unsigned short min_PIO_cycle_time_flow_control;
	unsigned short min_PIO_cycle_time_IORDY_flow_control;
	unsigned short reserved1[2];
	unsigned short reserved2[4];
	unsigned short queue_depth;
	unsigned short SATA_capabilities;
	unsigned short reserved3;
	unsigned short SATA_features_supported;
	unsigned short SATA_features_enabled;
	unsigned short major_version;
	unsigned short minor_version;
	unsigned short cmd_set0;
	unsigned short cmd_set1;
	unsigned short cmd_feature_sets_supported2;
	unsigned short cmd_feature_sets_supported3;
	unsigned short cmd_feature_sets_supported4;
	unsigned short cmd_feature_sets_supported5;
	unsigned short ultra_DMA_modes;
	unsigned short time_required_erase_cmd;
	unsigned short time_required_enhanced_cmd;
	unsigned short current_AAM_level_value;
	unsigned short master_password_identifier;
	unsigned short hardware_reset_result;
	unsigned short current_AAM_value;
	unsigned short stream_min_request_size;
	unsigned short streaming_transger_time_DMA;
	unsigned short streamimg_access_latency_DMA_PIO;
	unsigned short streaming_performance_granularity[2];
	unsigned short lba48sectors[4];
	unsigned short streaming_transfer_time_PIO;
	unsigned short reserved4;
	unsigned short physical_logical_sector_size;
	unsigned short inter_seek_delay;
	unsigned short world_wide_name[4];
	unsigned short reserved5[4];
	unsigned short reserved6;
	unsigned short words_per_logical_sector[2];
	unsigned short cmd_feature_supported;
	unsigned short cmd_feature_supported_enabled;
	unsigned short reserved7[6];
	unsigned short obsolete7;
	unsigned short security_status;
	unsigned short vendor_specific[31];
	unsigned short CFA_power_mode;
	unsigned short reserved8[7];
	unsigned short dev_from_factor;
	unsigned short reserved9[7];
	unsigned short current_media_serial_number[30];
	unsigned short SCT_cmd_transport;
	unsigned short reserved10[2];
	unsigned short alignment_logical_blocks_within_a_physical_block;
	unsigned short write_read_verify_sector_count_mode_3[2];
	unsigned short write_read_verify_sector_count_mode_2[2];
	unsigned short NV_cache_capabilities;
	unsigned short NV_cache_size[2];
	unsigned short nominal_media_rotation_rate;
	unsigned short reserved11;
	unsigned short NV_cache_options;
	unsigned short write_read_verify_feature_set_current_mode;
	unsigned short reserved12;
	unsigned short transport_major_version_number;
	unsigned short transport_minor_version_number;
	unsigned short reserved13[10];
	unsigned short min_blocks_per_cmd;
	unsigned short max_blocks_per_cmd;
	unsigned short reserved14[19];
	unsigned short integrity_word;
}__attribute__((packed));

struct ide_channel
{
	unsigned short base;
	char irq_num;
	struct _device_extension_s *device;
	char selected_drive;
};

typedef struct _device_extension_s
{
	char flag;
	char drive_num;
	char type;
	struct fifo *request_queue;
	struct ide_channel *channel;
	struct ide_identify_info *info;
	int int_flag;
	unsigned short capabilities;
	unsigned short signature;
	unsigned int command_sets;
	unsigned int size;
} device_extension_t;

static status_t ide_enter(driver_t *drv_obj);
static status_t ide_exit(driver_t *drv_obj);
status_t ide_read(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
status_t ide_write(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
void ide0_handler(int irq);
void ide1_handler(int irq);
void ide_reset_driver(struct ide_channel *channel);
void ide_select_device(device_extension_t *devext, char mode, unsigned char head);
int ide_pulling(struct ide_channel* channel, unsigned int advanced_check);
int ide_wait(device_extension_t *devext);
void ide_select_addressing_mode(device_extension_t *devext, unsigned int lba, unsigned char *mode, unsigned char *head, unsigned char *data);
void ide_select_sector(device_extension_t *devext, unsigned char mode, unsigned char *lba, unsigned int count);
void ide_select_cmd(unsigned char rw, unsigned char mode, unsigned char *cmd);
int AtaTypeTransfer(device_extension_t *devext, unsigned char rw, unsigned int lba, unsigned int count, void *buf);
int PioDataTransfer(device_extension_t *devext, unsigned char rw, unsigned char mode, unsigned char *buf, unsigned short count);

uint8_t disk_count;
struct ide_channel channels[2];

driver_func_t ide_driver = {
	.driver_enter = ide_enter,
	.driver_exit = ide_exit,
	.driver_open = NULL,
	.driver_close = NULL,
	.driver_read = ide_read,
	.driver_write = ide_write,
	.driver_devctl = NULL
};

#define DRV_NAME "General HardDisk Driver(IDE)"
#define DEV_NAME "hd"

static status_t ide_enter(driver_t *drv_obj)
{	
	int i;
	disk_count = *((unsigned char *)IDE_DISK_CNT);
	struct ide_channel *channel;
	int ide_channel_count = DIV_ROUND_UP(disk_count, 2);
	int channel_num = 0;
	int device_num = 0;
	int uninit_disk = disk_count;
	device_t *devobj;
	device_extension_t *devext;
	
	if (disk_count > 0)
	{
		while (channel_num < ide_channel_count)
		{
			channel = &channels[channel_num];
			switch (channel_num)
			{
			case 0:
				channel->base = ATA_PRIMARY_PORT;
				channel->irq_num = IDE0_IRQ;
				put_irq_handler(IDE0_IRQ, ide0_handler);
				irq_enable(IDE0_IRQ);
				
				break;
			case 1:
				channel->base = ATA_SECONDARY_PORT;
				channel->irq_num = IDE1_IRQ;
				put_irq_handler(IDE1_IRQ, ide0_handler);
				irq_enable(IDE1_IRQ);
				
				break;
			}
			channel->selected_drive = 0;
			
			while (device_num < 2 && uninit_disk)
			{
				char devname[DEVICE_MAX_NAME_LEN] = {0};
				sprintf(devname, "%s%d", DEV_NAME, channel_num*2 + device_num);
				device_create(drv_obj, sizeof(device_extension_t), devname, DEV_STORAGE, &devobj);
				devext = devobj->device_extension;
				channel->device = devext;
				
				devext->channel = channel;
				devext->drive_num = device_num;
				devext->type = IDE_ATA;
				devext->int_flag = 0;
				
				devext->info = kmalloc(sizeof(struct ide_identify_info));
				if (devext->info == NULL)
				{
					printk("kmalloc for ide device info falied!\n");
					continue;
				}
				
				ide_reset_driver(devext->channel);
				ide_select_device(devext, 0, 0);
				
				while (!(io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_READY)) __asm__("nop\n\t");
				
				IDE_SEND_CMD(channel, ATA_CMD_IDENTIFY);	//获取硬盘识别信息
				
				char err = ide_pulling(channel, 1);
				if (err)
				{
					printk("error! %d\n", err);
					continue;
				}
				port_insw(ATA_REG_DATA(devext->channel), devext->info, 256);
				
				devext->command_sets = (int)((devext->info->cmd_set1 << 16) + devext->info->cmd_set0);
				
				if (devext->command_sets & (1 << 26))
				{
					devext->size = ((unsigned int)devext->info->lba48sectors[1] << 16) + (unsigned int)devext->info->lba48sectors[0];
				}
				else
				{
					devext->size = ((unsigned int)devext->info->lba28sectors[1] << 16) + (unsigned int)devext->info->lba28sectors[0];
				}
				devext->size /= 2;
				
				devext->capabilities = devext->info->capabilities0;
				devext->signature = devext->info->general_config;
				devext->flag = 1;
				device_num++;
				uninit_disk--;
			}
			device_num = 0;
			channel_num++;
		}
	}
	
	return SUCCUESS;
}

static status_t ide_exit(driver_t *drv_obj)
{
	device_t *devobj, *next;
	device_extension_t *devext;
	// device_extension_t *ext;
	list_for_each_owner_safe(devobj, next, &drv_obj->device_list, list)
	{
		devext = devobj->device_extension;
		kfree(devext->info);
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

status_t ide_read(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size)
{
	int length = DIV_ROUND_UP(size, SECTOR_SIZE);
	char *tmp_buffer = kmalloc(length*SECTOR_SIZE);
	AtaTypeTransfer(dev->device_extension, IDE_READ, offset, length, tmp_buffer);
	memcpy(buf, tmp_buffer, size);
	kfree(tmp_buffer);
}

status_t ide_write(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size)
{
	int length = DIV_ROUND_UP(size, SECTOR_SIZE);
	char *tmp_buffer;
	if (size%SECTOR_SIZE == 0)
	{
		tmp_buffer = kmalloc(length*SECTOR_SIZE);
		AtaTypeTransfer(dev->device_extension, IDE_READ, offset + length - 1, 1, tmp_buffer);
		memcpy(tmp_buffer, buf, size);
	}
	else
	{
		tmp_buffer = buf;
		AtaTypeTransfer(dev->device_extension, IDE_READ, offset, length, tmp_buffer);
	}
	AtaTypeTransfer(dev->device_extension, IDE_WRITE, offset, length, tmp_buffer);
}

void ide_write_sector(device_extension_t *devext, uint32_t lba, char *buf)
{
	AtaTypeTransfer(devext, IDE_WRITE, lba, 1, buf);
}

void ide0_handler(int irq)
{
}

void ide1_handler(int irq)
{
}

void ide_reset_driver(struct ide_channel *channel)
{
	char ctrl = io_in8(ATA_REG_CTL(channel));
	io_out8(ATA_REG_CTL(channel), ctrl | (1 << 2));
	
	//等待重置
	int i;
	for (i = 0; i < 50; i++)
	{
		io_in8(ATA_REG_ALT_STATUS(channel));
	}
	io_out8(ATA_REG_CTL(channel), ctrl);
}

void ide_select_device(device_extension_t *devext, char mode, unsigned char head)
{
	io_out8(ATA_REG_DEVICE(devext->channel), (0xa0 | 0x40 | devext->drive_num << 4 | head));
	devext->channel->selected_drive = devext->drive_num;
}

int ide_pulling(struct ide_channel* channel, unsigned int advanced_check)
{
	int i;
	for(i = 0; i < 4; i++) {
		io_in8(ATA_REG_ALT_STATUS(channel));
 	}
    i = 10000;
    while ((io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_BUSY) && i--);
	
    if (advanced_check) {
		unsigned char state = io_in8(ATA_REG_STATUS(channel));

		if (state & ATA_STATUS_ERR)
		{
			return 2;
		}

		if (state & ATA_STATUS_DF)
		{
			return 1;
		}
	
		if ((state & ATA_STATUS_DRQ) == 0)
		{
			return 3;
		}
	}
	
	return 0;
}

int ide_wait(device_extension_t *devext)
{
	struct ide_channel *channel = devext->channel;
	while (io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_BUSY);
	return 0;
}

void ide_select_addressing_mode(device_extension_t *devext, unsigned int lba, unsigned char *mode, unsigned char *head, unsigned char *data)
{
	unsigned short cyl;
	unsigned char sector;
	if (lba >= 0x10000000 && devext->capabilities & 0x200)
	{
		*mode = 2;
		data[0] = lba & 0xff;
		data[1] = (lba >> 8) & 0xff;
		data[2] = (lba >> 16) & 0xff;
		data[3] = (lba >>24) & 0xff;
		data[4] = 0;
		data[5] = 0;
		*head = 0;
	}
	else if (devext->capabilities & 0x200)
	{
		*mode = 1;
		data[0] = lba & 0xff;
		data[1] = (lba >> 8) & 0xff;
		data[2] = (lba >> 16) & 0xff;
		data[3] = 0;
		data[4] = 0;
		data[5] = 0;
		*head = (lba >> 24) & 0x0f;
	}
	else
	{
		*mode = 0;
		sector = (lba % 63) + 1;
		cyl = (lba + 1 - sector) / (16 * 63);
		data[0] = sector;
		data[1] = (cyl >> 0) & 0xFF;
		data[2] = (cyl >> 8) & 0xFF;
		data[3] = 0;
		data[4] = 0;
		data[5] = 0;
		*head = (lba + 1 - sector) % (16 * 63) / (63);
	}
}

void ide_select_sector(device_extension_t *devext, unsigned char mode, unsigned char *lba, unsigned int count)
{
  	struct ide_channel *channel = devext->channel;

	/* 如果是LBA48就要写入24高端字节 */
	if (mode == 2) {
		io_out8(ATA_REG_FEATURE(channel), 0); // PIO mode.

		/* 写入要读写的扇区数*/
		io_out8(ATA_REG_SECTOR_CNT(channel), 0);

		/* 写入lba地址24~47位(即扇区号) */
		io_out8(ATA_REG_SECTOR_LOW(channel), lba[3]);
		io_out8(ATA_REG_SECTOR_MID(channel), lba[4]);
		io_out8(ATA_REG_SECTOR_HIGH(channel), lba[5]);
	}

	io_out8(ATA_REG_FEATURE(channel), 0); // PIO mode.

	/* 写入要读写的扇区数*/
	io_out8(ATA_REG_SECTOR_CNT(channel), count);

	/* 写入lba地址0~23位(即扇区号) */
	io_out8(ATA_REG_SECTOR_LOW(channel), lba[0]);
	io_out8(ATA_REG_SECTOR_MID(channel), lba[1]);
	io_out8(ATA_REG_SECTOR_HIGH(channel), lba[2]);
}

void ide_select_cmd(unsigned char rw, unsigned char mode, unsigned char *cmd)
{
#ifdef IDE_DMA_MODE
	if (mode == 0 && rw == IDE_READ) *cmd = ATA_CMD_READ_DMA;
	if (mode == 1 && rw == IDE_READ) *cmd = ATA_CMD_READ_DMA;
	if (mode == 2 && rw == IDE_READ) *cmd = ATA_CMD_READ_DMA_EXT;
	if (mode == 0 && rw == IDE_WRITE) *cmd = ATA_CMD_WRITE_DMA;
	if (mode == 1 && rw == IDE_WRITE) *cmd = ATA_CMD_WRITE_DMA;
	if (mode == 2 && rw == IDE_WRITE) *cmd = ATA_CMD_WRITE_DMA_EXT;
#else
	if (mode == 0 && rw == IDE_READ) *cmd = ATA_CMD_READ_PIO;
	if (mode == 1 && rw == IDE_READ) *cmd = ATA_CMD_READ_PIO;   
	if (mode == 2 && rw == IDE_READ) *cmd = ATA_CMD_READ_PIO_EXT;
	if (mode == 0 && rw == IDE_WRITE) *cmd = ATA_CMD_WRITE_PIO;
	if (mode == 1 && rw == IDE_WRITE) *cmd = ATA_CMD_WRITE_PIO;
	if (mode == 2 && rw == IDE_WRITE) *cmd = ATA_CMD_WRITE_PIO_EXT;
#endif
}

int PioDataTransfer(device_extension_t *devext, unsigned char rw, unsigned char mode, unsigned char *buf, unsigned short count)
{
	short i;
	unsigned char error;
	if (rw == IDE_READ) {	
		#ifdef _DEBUG_IDE
			printk("PIO read->");
		#endif
		for (i = 0; i < count; i++) {
			/* 醒来后开始执行下面代码*/
			if ((error = ide_wait(devext))) {     //  若失败
				/* 重置磁盘驱动并返回 */
				ide_reset_driver(devext);
				return error;
			}
			port_insw(ATA_REG_DATA(devext->channel), buf, 256);
			buf += SECTOR_SIZE;
		}
	} else {
		#ifdef _DEBUG_IDE
			printk("PIO write->");
		#endif
		for (i = 0; i < count; i++) {
			/* 等待硬盘控制器请求数据 */
			if ((error = ide_wait(devext))) {     //  若失败
				/* 重置磁盘驱动并返回 */
				ide_reset_driver(devext);
				return error;
			}
			/* 把数据写入端口，完成1个扇区后会产生一次中断 */
			port_outsw(ATA_REG_DATA(devext->channel), buf, 256);
			buf += SECTOR_SIZE;
            //printk("write success! ");
		}
		/* 刷新写缓冲区 */
		io_out8(ATA_REG_CMD(devext->channel), mode > 1 ?
			ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
		ide_pulling(devext->channel, 0);
	}
	return 0;
}

int AtaTypeTransfer(device_extension_t *devext, unsigned char rw, unsigned int lba, unsigned int count, void *buf)
{
	unsigned char mode;	/* 0: CHS, 1:LBA28, 2: LBA48 */
	unsigned char dma; /* 0: No DMA, 1: DMA */
	unsigned char cmd;	
	unsigned char *_buf = (unsigned char *)buf;

	unsigned char lbaIO[6];	/* 由于最大是48位，所以这里数组的长度为6 */

	struct ide_channel *channel = devext->channel;

   	unsigned char head, err;

	/* 要去操作的扇区数 */
	unsigned int todo;
	/* 已经完成的扇区数 */
	unsigned int done = 0;
	
	while (done < count) {
		/* 获取要去操作的扇区数
		由于一次最大只能操作256个扇区，这里用256作为分界
		 */
		if ((done + 256) <= count) {
			todo = 256;
		} else {
			todo = count - done;
		}

		/* 选择传输模式（PIO或DMA） */
		dma = 0; // We don't support DMA

		/* 选择寻址模式 */
		// (I) Select one from LBA28, LBA48 or CHS;
		ide_select_addressing_mode(devext, lba + done, &mode, &head, lbaIO);

		/* 等待驱动不繁忙 */
		// (III) Wait if the drive is busy;
		while (io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_BUSY);
		/* 从控制器中选择设备 */
		ide_select_device(devext, mode, head);

		/* 填写参数，扇区和扇区数 */
		ide_select_sector(devext, mode, lbaIO, count);

		/* 等待磁盘控制器处于准备状态 */
		while (!(io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_READY));

		/* 选择并发送命令 */
		ide_select_cmd(rw, mode, &cmd);

		#ifdef _DEBUG_IDE	
			printk("lba mode %d num %d io %d %d %d %d %d %d->",
				mode, lba, lbaIO[0], lbaIO[1], lbaIO[2], lbaIO[3], lbaIO[4], lbaIO[5]);
			printk("rw %d dma %d cmd %x head %d\n",
				rw, dma, cmd, head);
		#endif
		/* 等待磁盘控制器处于准备状态 */
		while (!(io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_READY));

		/* 发送命令 */
		IDE_SEND_CMD(channel, cmd);

		/* 根据不同的模式传输数据 */
		if (dma) {	/* DMA模式 */
			if (rw == IDE_READ) {
				// DMA Read.
			} else {
				// DMA Write.
			}
		} else {
			/* PIO模式数据传输 */
			if ((err = PioDataTransfer(devext, rw, mode, _buf, todo))) {
				return err;
			}
			_buf += todo * SECTOR_SIZE;
			done += todo;
		}
	}

	return 0;
}

static __init void ide_driver_entry(void)
{
	if (driver_create(ide_driver, DRV_NAME) < 0)
	{
		printk(COLOR_RED"[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(ide_driver_entry);
