#ifndef _DISK_MBR_H
#define _DISK_MBR_H

#include <stdint.h>
#include <types.h>

typedef struct MBRPartitionEntry {
	uint8_t	 sign;
	uint8_t	 start_chs[3];
	uint8_t	 fs_type;
	uint8_t	 end_chs[3];
	uint32_t start_lba;
	uint32_t size;
} __attribute__((packed)) MBRPartitionEntry;

struct StorageDevice;
bool disk_is_mbr(struct StorageDevice *storage_device);
void parse_mbr_partition_table(struct StorageDevice *storage_device);

#endif