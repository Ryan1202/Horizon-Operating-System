#ifndef _DISK_H
#define _DISK_H

#include "objects/handle.h"
#include "objects/transfer.h"
#include "stdint.h"
#include <stdint.h>

struct Object;
struct MBRPartitionEntry;
typedef struct Partition {
	enum {
		PARTITION_TYPE_MBR,
		PARTITION_TYPE_GPT,
	} type;
	union {
		struct MBRPartitionEntry *mbr;
	};
	size_t	start_lba;
	size_t	size_lba;
	uint8_t index;

	uint8_t *superblock;

	struct Object *object;
	struct Object *storage_object;
} Partition;

TransferResult disk_transfer_in(
	struct Object *object, ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, uint32_t position, size_t count);
TransferResult disk_transfer_in_async(
	struct Object *object, ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, uint32_t position, size_t count,
	void **handle);
TransferResult disk_transfer_out(
	struct Object *object, ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, uint32_t position, size_t count);
TransferResult disk_transfer_out_async(
	struct Object *object, ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, uint32_t position, size_t count,
	void **handle);
TransferResult disk_is_transfer_in_done(
	struct Object *object, void **handle, bool *is_done);
TransferResult disk_is_transfer_out_done(
	struct Object *object, void **handle, bool *is_done);

#endif