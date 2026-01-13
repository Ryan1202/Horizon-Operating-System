#ifndef _STORAGE_IO_H
#define _STORAGE_IO_H

#include "driver/storage/storage_io_queue.h"
#include <objects/handle.h>
#include <objects/object.h>
#include <objects/transfer.h>

TransferResult storage_transfer_async(
	Object *object, ObjectHandle *obj_handle, TransferDirection direction,
	uint8_t *buf, uint32_t position, size_t count, void **handle);
TransferResult storage_transfer(
	Object *object, ObjectHandle *obj_handle, TransferDirection direction,
	uint8_t *buf, uint32_t position, size_t count);
TransferResult storage_is_transfer_done(
	Object *object, void **handle, bool *done);

struct StorageDevice;
struct StorageRequest;
bool storage_check_request_size(
	struct StorageDevice *device, struct StorageRequest *request);

#endif