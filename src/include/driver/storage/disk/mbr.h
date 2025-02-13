#ifndef _DISK_MBR_H
#define _DISK_MBR_H

#include <driver/storage/storage_dm.h>

typedef struct {
	uint8_t	 sign;
	uint8_t	 start_chs[3];
	uint8_t	 fs_type;
	uint8_t	 end_chs[3];
	uint32_t start_lba;
	uint32_t size;
} __attribute__((packed)) MBRPartitionEntry;

bool disk_is_mbr(StorageDevice *storage_device);
void parse_mbr_partition_table(StorageDevice *storage_device);

#endif