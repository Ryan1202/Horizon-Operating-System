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

TransferResult disk_transfer_in(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle);
TransferResult disk_transfer_out(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle);
TransferResult disk_is_transfer_in_done(
	Object *object, void **handle, bool *is_done);
TransferResult disk_is_transfer_out_done(
	Object *object, void **handle, bool *is_done);

#endif