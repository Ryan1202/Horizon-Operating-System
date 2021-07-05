#ifndef _IDE_H
#define _IDE_H

#include <kernel/fifo.h>
#include <stdint.h>

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

struct ide_channel
{
	unsigned short base;
	char irq_num;
	struct ide_device *devices;
	char selected_drive;
};

struct ide_device
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
};

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

void init_ide(void);
void ide_delete(int disk_num);
void ide_read_sector(int disk_num, uint32_t lba, char *buf);
void ide_write_sector(int disk_num, uint32_t lba, char *buf);
void ide_probe(char disk_count);
void ide_reset_driver(struct ide_channel *channel);
void ide_select_device(struct ide_device *device, char mode, unsigned char head);
int ide_pulling(struct ide_channel* channel, unsigned int advanced_check);
int ide_wait(struct ide_device *device);
void ide_select_addressing_mode(struct ide_device *device, unsigned int lba, unsigned char *mode, unsigned char *head, unsigned char *data);
void ide_select_sector(struct ide_device *device, unsigned char mode, unsigned char *lba, unsigned int count);
void ide_select_cmd(unsigned char rw, unsigned char mode, unsigned char *cmd);
int AtaTypeTransfer(struct ide_device *dev, unsigned char rw, unsigned int lba, unsigned int count, void *buf);
int PioDataTransfer(struct ide_device *dev, unsigned char rw, unsigned char mode, unsigned char *buf, unsigned short count);
void ide0_handler(int irq);
void ide1_handler(int irq);

#endif