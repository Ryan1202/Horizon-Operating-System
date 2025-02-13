#include <driver/storage/disk/disk.h>
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io.h>
#include <objects/transfer.h>

TransferResult disk_transfer_in(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle) {
	Partition *partition = object->value.partition;

	return TRANSFER_IN_BLOCK(partition->storage_object)(
		partition->storage_object, direction, buf,
		position + partition->start_lba, count, handle);
}

TransferResult disk_transfer_out(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle) {
	Partition *partition = object->value.partition;

	return TRANSFER_OUT_BLOCK(partition->storage_object)(
		partition->storage_object, direction, buf,
		position + partition->start_lba, count, handle);
}

TransferResult disk_is_transfer_in_done(
	Object *object, void **handle, bool *is_done) {
	Partition *partition = object->value.partition;

	return TRANSFER_IN_IS_DONE(partition->storage_object)(
		partition->storage_object, handle, is_done);
}

TransferResult disk_is_transfer_out_done(
	Object *object, void **handle, bool *is_done) {
	Partition *partition = object->value.partition;

	return TRANSFER_OUT_IS_DONE(partition->storage_object)(
		partition->storage_object, handle, is_done);
}
