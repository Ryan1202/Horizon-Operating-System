#include <device/ide.h>
#include <device/pci.h>
#include <device/apic.h>
#include <kernel/console.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <kernel/descriptor.h>
#include <kernel/fifo.h>
#include <kernel/fs/fs.h>
#include <device/disk.h>
#include <config.h>
#include <types.h>
#include <math.h>

uint8_t disk_count;
struct ide_channel channels[2];
struct ide_device devices[4];
struct fifo ide_request_queue;
static char reqbuf[512];

void init_ide(void)
{	
	int i;
	disk_count = *((unsigned char *)IDE_DISK_CNT);
	
	if (disk_count > 0)
	{
		ide_probe(disk_count);
		fifo_init(&ide_request_queue, 512, reqbuf);
		for (i = 0; i < disk_count; i++)
		{
			struct disk *disk = add_disk("IDE HardDisk");
			disk->disk_read = ide_read_sector;
			disk->disk_write = ide_write_sector;
			disk->disk_delete = ide_delete;
			init_fs(disk, i);
		}
	}
}

void ide_delete(int disk_num)
{
	kfree(devices[disk_num].info);
	return;
}

void ide_read_sector(int disk_num, uint32_t lba, char *buf)
{
	AtaTypeTransfer(&devices[disk_num], IDE_READ, lba, 1, buf);
}

void ide_write_sector(int disk_num, uint32_t lba, char *buf)
{
	AtaTypeTransfer(&devices[disk_num], IDE_WRITE, lba, 1, buf);
}

void ide_probe(char disk_count)
{
	struct ide_channel *channel;
	int ide_channel_count = DIV_ROUND_UP(disk_count, 2);
	int channel_num = 0;
	int device_num = 0;
	int uninit_disk = disk_count;
	
	while (channel_num < ide_channel_count)
	{
		channel = &channels[channel_num];
		switch (channel_num)
		{
		case 0:
			channel->base = ATA_PRIMARY_PORT;
			channel->irq_num = 14;
			put_irq_handler(IDE0_IRQ, ide0_handler);
			irq_enable(IDE0_IRQ);
			
			break;
		case 1:
			channel->base = ATA_SECONDARY_PORT;
			channel->irq_num = 15;
			put_irq_handler(IDE1_IRQ, ide0_handler);
			irq_enable(IDE1_IRQ);
			
			break;
		}
		channel->selected_drive = 0;
		
		while (device_num < 2 && uninit_disk)
		{
			if (channel_num == ATA_SECONDARY)
			{
				channel->devices = &devices[2];
			}
			else
			{
				channel->devices = &devices[0];
			}
			
			struct ide_device *device = &channel->devices[device_num];
			device->channel = channel;
			device->drive_num = device_num;
			device->type = IDE_ATA;
			device->int_flag = 0;
			
			device->info = kmalloc(512);
			if (device->info == NULL)
			{
				printk("kmalloc for ide device info falied!\n");
				continue;
			}
			
			ide_reset_driver(device->channel);
			ide_select_device(device, 0, 0);
			
			while (!(io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_READY)) __asm__("nop\n\t");
			
			IDE_SEND_CMD(channel, ATA_CMD_IDENTIFY);	//获取硬盘识别信息
			
			char err = ide_pulling(channel, 1);
			if (err)
			{
				printk("error! %d\n", err);
				continue;
			}
			port_insw(ATA_REG_DATA(device->channel), device->info, 256);
			
			device->command_sets = (int)((device->info->cmd_set1 << 16) + device->info->cmd_set0);
			
			if (device->command_sets & (1 << 26))
			{
				device->size = ((unsigned int)device->info->lba48sectors[1] << 16) + (unsigned int)device->info->lba48sectors[0];
			}
			else
			{
				device->size = ((unsigned int)device->info->lba28sectors[1] << 16) + (unsigned int)device->info->lba28sectors[0];
			}
			device->size /= 2;
			
			device->capabilities = device->info->capabilities0;
			device->signature = device->info->general_config;
			device->flag = 1;
			device_num++;
			uninit_disk--;
		}
		device_num = 0;
		channel_num++;
	}
}

void ide0_handler(int irq)
{
	printk("ide0 interrupt\n");
}

void ide1_handler(int irq)
{
	printk("ide0 interrupt\n");
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

void ide_select_device(struct ide_device *device, char mode, unsigned char head)
{
	io_out8(ATA_REG_DEVICE(device->channel), (0xa0 | 0x40 | device->drive_num << 4 | head));
	device->channel->selected_drive = device->drive_num;
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

int ide_wait(struct ide_device *device)
{
	struct ide_channel *channel = device->channel;
	while (io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_BUSY);
	return 0;
}

void ide_select_addressing_mode(struct ide_device *device, unsigned int lba, unsigned char *mode, unsigned char *head, unsigned char *data)
{
	unsigned short cyl;
	unsigned char sector;
	if (lba >= 0x10000000 && device->capabilities & 0x200)
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
	else if (device->capabilities & 0x200)
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

void ide_select_sector(struct ide_device *device, unsigned char mode, unsigned char *lba, unsigned int count)
{
  	struct ide_channel *channel = device->channel;

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

int PioDataTransfer(struct ide_device *dev, unsigned char rw, unsigned char mode, unsigned char *buf, unsigned short count)
{
	short i;
	unsigned char error;
	if (rw == IDE_READ) {	
		#ifdef _DEBUG_IDE
			printk("PIO read->");
		#endif
		for (i = 0; i < count; i++) {
			/* 醒来后开始执行下面代码*/
			if ((error = ide_wait(dev))) {     //  若失败
				/* 重置磁盘驱动并返回 */
				ide_reset_driver(dev);
				return error;
			}
			port_insw(ATA_REG_DATA(dev->channel), buf, 256);
			buf += SECTOR_SIZE;
		}
	} else {
		#ifdef _DEBUG_IDE
			printk("PIO write->");
		#endif
		for (i = 0; i < count; i++) {
			/* 等待硬盘控制器请求数据 */
			if ((error = ide_wait(dev))) {     //  若失败
				/* 重置磁盘驱动并返回 */
				ide_reset_driver(dev);
				return error;
			}
			/* 把数据写入端口，完成1个扇区后会产生一次中断 */
			port_outsw(ATA_REG_DATA(dev->channel), buf, 256);
			buf += SECTOR_SIZE;
            //printk("write success! ");
		}
		/* 刷新写缓冲区 */
		io_out8(ATA_REG_CMD(dev->channel), mode > 1 ?
			ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
		ide_pulling(dev->channel, 0);
	}
	return 0;
}

int AtaTypeTransfer(struct ide_device *dev, unsigned char rw, unsigned int lba, unsigned int count, void *buf)
{
	unsigned char mode;	/* 0: CHS, 1:LBA28, 2: LBA48 */
	unsigned char dma; /* 0: No DMA, 1: DMA */
	unsigned char cmd;	
	unsigned char *_buf = (unsigned char *)buf;

	unsigned char lbaIO[6];	/* 由于最大是48位，所以这里数组的长度为6 */

	struct ide_channel *channel = dev->channel;

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
		ide_select_addressing_mode(dev, lba + done, &mode, &head, lbaIO);

		/* 等待驱动不繁忙 */
		// (III) Wait if the drive is busy;
		while (io_in8(ATA_REG_STATUS(channel)) & ATA_STATUS_BUSY);
		/* 从控制器中选择设备 */
		ide_select_device(dev, mode, head);

		/* 填写参数，扇区和扇区数 */
		ide_select_sector(dev, mode, lbaIO, count);

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
			if ((err = PioDataTransfer(dev, rw, mode, _buf, todo))) {
				return err;
			}
			_buf += todo * SECTOR_SIZE;
			done += todo;
		}
	}

	return 0;
}
