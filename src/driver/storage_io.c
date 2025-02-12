/**
 * 默认的存储设备IO实现
 */
#include "kernel/device.h"
#include <driver/storage_dm.h>
#include <driver/storage_io.h>
#include <driver/storage_io_queue.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <objects/transfer.h>

TransferResult storage_transfer(
	Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle) {
	Device		   *device	= object->value.device;
	StorageRequest *request = kmalloc(sizeof(StorageRequest));

	request->storage_device = device->device_manager_extension;
	request->rw				= (direction == TRANSFER_IN) ? 0 : 1;
	request->buf			= buf;
	request->position		= position;
	request->count			= count;
	request->is_finished	= 0;

	*handle = (void *)request;

	storage_add_request(device->device_manager_extension, request);
	return TRANSFER_OK;
}

TransferResult storage_is_transfer_done(
	Object *object, void **handle, bool *done) {
	if (object->type != OBJECT_TYPE_DEVICE) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	Device *device = object->value.device;

	if (device->device_driver->type != DEVICE_TYPE_STORAGE) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	StorageDevice *storage_device = device->device_manager_extension;

	StorageRequest *req = *handle;
	if (req->storage_device != storage_device || done == NULL) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	*done = req->is_finished;

	return TRANSFER_OK;
}