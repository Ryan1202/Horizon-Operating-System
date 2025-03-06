#ifndef _STORAGE_IO_H
#define _STORAGE_IO_H

#include <objects/object.h>
#include <objects/transfer.h>

TransferResult storage_transfer_async(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle);
TransferResult storage_transfer(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count);
TransferResult storage_is_transfer_done(
	Object *object, void **handle, bool *done);

#endif