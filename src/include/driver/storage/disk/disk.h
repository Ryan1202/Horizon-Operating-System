#ifndef _DISK_H
#define _DISK_H

#include "stdint.h"
#include <driver/storage/disk/mbr.h>

typedef struct Partition {
	enum {
		PARTITION_TYPE_MBR,
		PARTITION_TYPE_GPT,
	} type;
	union {
		MBRPartitionEntry *mbr;
	};
	size_t start_lba;
	size_t size_lba;

	Object *storage_object;
} Partition;

#endif