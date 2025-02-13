#include "driver/storage/storage_io.h"
#include <driver/storage/disk/disk.h>
#include <driver/storage/storage_dm.h>
#include <objects/transfer.h>

TransferResult disk_transfer(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle) {
	Partition *partition = object->value.partition;

	return TRANSFER_IN_BLOCK(partition->storage_object)(
		partition->storage_object, direction, buf,
		position + partition->start_lba, count, handle);
}