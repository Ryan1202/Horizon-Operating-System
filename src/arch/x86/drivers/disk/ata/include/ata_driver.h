#ifndef _ATA_DRIVER_H
#define _ATA_DRIVER_H

#include "bits.h"
#include <stdint.h>

#define ATA_PRIMARY_PORT   0x1f0
#define ATA_SECONDARY_PORT 0x170

#define ATA_PRIMARY_CONTROL_PORT   0x3f6
#define ATA_SECONDARY_CONTROL_PORT 0x376

typedef enum AtaDeviceType {
	ATA_DEVICE_TYPE_ATA,
	ATA_DEVICE_TYPE_ATAPI,
} AtaDeviceType;

#define ATA_STATUS_BUSY	 0x80 // 驱动器忙
#define ATA_STATUS_READY 0x40 // 驱动器就绪
#define ATA_STATUS_DF	 0x20 // 驱动器写故障
#define ATA_STATUS_SEEK	 0x10 // 驱动器查找完成
#define ATA_STATUS_DRQ	 0x08 // 请求数据
#define ATA_STATUS_CORR	 0x04 // Correct data
#define ATA_STATUS_INDEX 0x02 // 索引
#define ATA_STATUS_ERR	 0x01 // 错误

#define ATA_ERROR_BBK	0x80 // 错误块
#define ATA_ERROR_UNC	0x40 // 无法矫正的数据
#define ATA_ERROR_MC	0x20 // 媒体更改
#define ATA_ERROR_IDNF	0x10 // 找不到ID标记
#define ATA_ERROR_MCR	0x08 // 媒体更改请求
#define ATA_ERROR_ABRT	0x04 // 命令终止
#define ATA_ERROR_TK0NF 0x02 // 找不到磁道0
#define ATA_ERROR_AMNF	0x01 // 没有地址标记

#define ATA_CONTROL_NIEN BIT(1) // 禁止中断
#define ATA_CONTROL_SRST BIT(2) // 软复位
#define ATA_CONTROL_HOB	 BIT(7) // 高字节优先(大端字节序)

#define ATA_REG_DATA	  0x00
#define ATA_REG_ERROR	  0x01
#define ATA_REG_FEATURES  0x01
#define ATA_REG_SECCOUNT0 0x02
#define ATA_REG_LBA0	  0x03
#define ATA_REG_LBA1	  0x04
#define ATA_REG_LBA2	  0x05
#define ATA_REG_HDDEV_SEL 0x06
#define ATA_REG_COMMAND	  0x07
#define ATA_REG_STATUS	  0x07

#define ATA_REG_SECCOUNT1 0x02
#define ATA_REG_LBA3	  0x03
#define ATA_REG_LBA4	  0x04
#define ATA_REG_LBA5	  0x05

#define ATA_REG_CONTROL	   0x02
#define ATA_REG_ALTSTATUS  0x02
#define ATA_REG_DEVADDRESS 0x03

#define ATA_PRIMARY	  0x00
#define ATA_SECONDARY 0x01

#define ATA_MASTER 0x00
#define ATA_SLAVE  0x01

typedef struct AtaIdentifyInfo {
	struct {
		uint8_t reserved1			: 1;
		uint8_t retired1			: 1;
		uint8_t response_incomplete : 1;
		uint8_t retired2			: 3;
		uint8_t obsolete			: 1;
		uint8_t removable			: 1;
		uint8_t retired3			: 7;
		uint8_t device_type			: 1;
	} general_config;
	uint16_t obsolete1;
	uint16_t specific_config;
	uint16_t obsolete2;
	uint32_t retired1;
	uint16_t obsolete3;
	uint32_t reserved1;
	uint16_t retired2;
	uint8_t	 serial_number[20];
	uint32_t retired3;
	uint16_t obsolete4;
	uint8_t	 firmware_revision[8];
	uint8_t	 model_number[40];
	uint8_t	 max_block_transfer;
	uint8_t	 reserved2;
	uint16_t reserved3;
	struct {
		uint8_t retired;
		uint8_t dma_supported			: 1;
		uint8_t lba_supported			: 1;
		uint8_t ioready_disabled		: 1;
		uint8_t ioready_supported		: 1;
		uint8_t reserved1				: 1;
		uint8_t standby_timer_supported : 1;
		uint8_t reserved2				: 2;
	} capabilities;
	uint16_t reserved4;
	uint32_t obsolete5;
	uint16_t translation_field_valid : 3;
	uint16_t reserved5				 : 13;
	uint16_t obsolete6[5];
	uint8_t	 current_block;
	uint8_t	 multiple_sector_valid : 1;
	uint8_t	 reserved6			   : 7;
	uint32_t total_sectors;
	uint16_t obsolete7;
	struct {
		uint8_t dma_modes_supported : 3;
		uint8_t reserved1			: 5;
		uint8_t dma_modes_select	: 3;
		uint8_t reserved2			: 5;
	} multiword_dma;
	uint8_t	 pio_modes_supported;
	uint8_t	 reserved7;
	uint16_t min_multiword_dma_cycle_time;
	uint16_t recommended_multiword_dma_cycle_time;
	uint16_t min_pio_cycle_time;
	uint16_t min_pio_cycle_time_ioready;
	uint32_t reserved8;
	uint16_t reserved9[4];
	uint16_t max_queue_depth : 5;
	uint16_t reserved10		 : 11;
	uint16_t reserved_for_sata[4];
	uint16_t major_version;
	uint16_t minor_version;
	struct {
		uint8_t smart_supported										  : 1;
		uint8_t security_supported									  : 1;
		uint8_t removable_media										  : 1;
		uint8_t power_management_supported							  : 1;
		uint8_t reserved1											  : 1;
		uint8_t write_cache_supported								  : 1;
		uint8_t look_ahead_supported								  : 1;
		uint8_t release_interrupt_supported							  : 1;
		uint8_t service_interrupt_supported							  : 1;
		uint8_t device_reset_supported								  : 1;
		uint8_t host_protected_area_supported						  : 1;
		uint8_t obsolete1											  : 1;
		uint8_t write_buffer_command_supported						  : 1;
		uint8_t read_buffer_command_supported						  : 1;
		uint8_t nop_command_supported								  : 1;
		uint8_t obsolete											  : 1;
		uint8_t download_microcode_supported						  : 1;
		uint8_t rw_dma_queued_supported								  : 1;
		uint8_t cfa_supported										  : 1;
		uint8_t advanced_power_management							  : 1;
		uint8_t removable_media_status_notification_supported		  : 1;
		uint8_t powerup_in_standby_supported						  : 1;
		uint8_t set_features_required_to_spinup_after_powerup		  : 1;
		uint8_t obsolete2											  : 1;
		uint8_t set_max_security_supported							  : 1;
		uint8_t auto_acoustic_management_supported					  : 1;
		uint8_t addressing_48bit_supported							  : 1;
		uint8_t device_config_overlay_supported						  : 1;
		uint8_t mandatory_flush_cache_supported						  : 1;
		uint8_t flush_cache_ext_supported							  : 1;
		uint8_t reserved2											  : 2;
		uint8_t smart_error_log_supported							  : 1;
		uint8_t smart_self_test_supported							  : 1;
		uint8_t media_serial_number_supported						  : 1;
		uint8_t media_card_pass_through_supported					  : 1;
		uint8_t streaming_feature_supported							  : 1;
		uint8_t general_purpose_logging_supported					  : 1;
		uint8_t write_fua_ext_supported								  : 1;
		uint8_t write_queued_fua_ext_supported						  : 1;
		uint8_t name_64bit_supported								  : 1;
		uint8_t urg_gor_read_stream_dma_supported					  : 1;
		uint8_t urg_for_write_stream_dma_supported					  : 1;
		uint8_t reserved3											  : 2;
		uint8_t idle_immediate_supported							  : 1;
		uint8_t reserved4											  : 2;
		uint8_t smart_enabled										  : 1;
		uint8_t security_mode_enabled								  : 1;
		uint8_t removablemedia_enabled								  : 1;
		uint8_t power_management_enabled							  : 1;
		uint8_t reserved5											  : 1;
		uint8_t write_cache_enabled									  : 1;
		uint8_t look_ahead_enabled									  : 1;
		uint8_t release_interrupt_enabled							  : 1;
		uint8_t service_interrupt_enabled							  : 1;
		uint8_t device_reset_enabled								  : 1;
		uint8_t host_protected_area_enabled							  : 1;
		uint8_t obsolete3											  : 1;
		uint8_t write_buffer_command_enabled						  : 1;
		uint8_t read_buffer_command_enabled							  : 1;
		uint8_t nop_command_enabled									  : 1;
		uint8_t obsolete4											  : 1;
		uint8_t download_microcode_enabled							  : 1;
		uint8_t rw_dma_queued_enabled								  : 1;
		uint8_t cfa_enabled											  : 1;
		uint8_t advanced_power_management_enabled					  : 1;
		uint8_t removable_media_status_notification_enabled			  : 1;
		uint8_t powerup_in_standby_enabled							  : 1;
		uint8_t set_features_required_to_spinup_after_powerup_enabled : 1;
		uint8_t obsolete5											  : 1;
		uint8_t set_max_security_enabled							  : 1;
		uint8_t auto_acoustic_management_enabled					  : 1;
		uint8_t addressing_48bit_enabled							  : 1;
		uint8_t device_config_overlay_enabled						  : 1;
		uint8_t mandatory_flush_cache_enabled						  : 1;
		uint8_t flush_cache_ext_enabled								  : 1;
		uint8_t reserved6											  : 2;
		uint8_t smart_error_log_enabled								  : 1;
		uint8_t smart_self_test_enabled								  : 1;
		uint8_t media_serial_number_enabled							  : 1;
		uint8_t media_card_pass_through_enabled						  : 1;
		uint8_t streaming_feature_enabled							  : 1;
		uint8_t general_purpose_logging_enabled						  : 1;
		uint8_t write_fua_ext_enabled								  : 1;
		uint8_t write_queued_fua_ext_enabled						  : 1;
		uint8_t reserved7											  : 2;
		uint8_t idle_immediate_enabled								  : 1;
		uint8_t reserved8											  : 2;
	} command_set_feature_support;
	uint8_t	 udma_mode_support;
	uint8_t	 udma_mode_select;
	uint16_t time_required_for_security_erase;
	uint16_t time_required_for_enhanced_security_erase;
	uint16_t current_advanced_power_management_value;
	uint16_t master_password_revision_code;
	uint16_t hardware_reset_result;
	uint8_t	 current_AAM_value;
	uint8_t	 recommended_AAM_value;
	uint16_t stream_min_request_size;
	uint16_t streaming_transfer_time_DMA;
	uint16_t streaming_access_latency_DMA_PIO;
	uint16_t streaming_performance_granularity[2];
	uint16_t max_lba48_addressable_sectors[4];
	uint16_t streaming_transfer_time_PIO;
	uint16_t reserved11;
	uint16_t physical_logical_sector_size			  : 4;
	uint16_t reserved12								  : 8;
	uint16_t device_logical_sector_is_longer_than_256 : 1;
	uint16_t device_has_multiple_logical_sectors	  : 1;
	uint16_t reserved13								  : 2;
	uint16_t inter_seek_delay;
	uint32_t unique_id1;
	uint16_t unique_id2 : 4;
	uint32_t ieee_oui	: 24;
	uint16_t naa		: 4;
	uint16_t world_wide_name[4];
	uint16_t reserved14;
	uint16_t words_per_logical_sector[2];
	uint16_t reserved15[8];
	uint16_t removable_media_status;
	uint16_t security_status;
	uint16_t vendor_specific[31];
	uint16_t CFA_power_mode;
	uint16_t reserved16[15];
	uint8_t	 current_media_serial_number[60];
	uint16_t reserved17[49];
	uint16_t integrity_word;
} __attribute__((packed)) AtaIdentifyInfo;

extern struct Driver ata_driver;

#endif