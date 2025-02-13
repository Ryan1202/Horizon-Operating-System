#ifndef _DISK_MBR_H
#define _DISK_MBR_H

#include <driver/storage/storage_dm.h>

typedef struct {
	uint8_t	 boot_indicator;
	uint8_t	 starting_head;
	uint16_t starting_sector_cylinder;
	uint8_t	 system_id;
	uint8_t	 ending_head;
	uint16_t ending_sector_cylinder;
	uint32_t starting_lba;
	uint32_t size_in_lba;
} __attribute__((packed)) MBRPartitionEntry;

bool disk_is_mbr(StorageDevice *storage_device);
void parse_mbr_partition_table(StorageDevice *storage_device);

#endif