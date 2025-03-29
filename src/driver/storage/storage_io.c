/**
 * 默认的存储设备IO实现
 */
#include "kernel/device.h"
#include "kernel/driver.h"
#include "kernel/thread.h"
#include "multiple_return.h"
#include "stdint.h"
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io.h>
#include <driver/storage/storage_io_queue.h>
#include <kernel/device_driver.h>
#include <kernel/memory.h>
#include <math.h>
#include <objects/object.h>
#include <objects/transfer.h>

// 检查请求大小是否超过设备允许的最大请求大小
bool storage_check_request_size(
	StorageDevice *device, StorageRequest *request) {
	if (request->count > device->max_block_per_request) { return false; }
	return true;
}

// 将过大的请求分割成多个小请求
DriverResult storage_generate_request(
	StorageDevice *device, int rw, void *buf, size_t position, size_t count,
	DEF_MRET(StorageRequest *, last_request)) {
	// 计算需要分割成几个请求
	uint32_t num_requests = DIV_ROUND_UP(count, device->max_block_per_request);
	uint32_t remaining_blocks = count;
	uint32_t current_position = position;

	StorageRequest *first_request = NULL;
	StorageRequest *request;
	struct task_s  *cur_thread = get_current_thread();

	// uint32_t t, t0, t1, t2, t3;
	// 分割请求
	for (uint32_t i = 0; i < num_requests; i++) {
		// 计算当前分片的大小
		uint32_t current_count =
			MIN(remaining_blocks, device->max_block_per_request);

		// 创建新的请求
		request = kmalloc(sizeof(StorageRequest));
		if (request == NULL && first_request != NULL) {
			return DRIVER_RESULT_OUT_OF_MEMORY;
		}

		// 设置新请求的参数
		request->position			 = current_position;
		request->count				 = current_count;
		request->rw					 = rw;
		request->is_finished		 = 0;
		request->storage_device		 = device;
		request->next_merged_request = NULL;
		request->thread				 = cur_thread;

		// 分配或指向原始缓冲区中对应的部分
		request->buf =
			buf + i * device->max_block_per_request * device->block_size;

		storage_add_request(device, request);

		// 更新剩余块和当前位置
		remaining_blocks -= current_count;
		current_position += current_count;
		if (first_request == NULL) { first_request = request; }
	}
	MRET(last_request) = request;
	return DRIVER_RESULT_OK;
}

TransferResult storage_transfer_async(
	Object *object, ObjectHandle *obj_handle, TransferDirection direction,
	uint8_t *buf, uint32_t position, size_t count, void **handle) {
	while (object->attr->type == OBJECT_TYPE_SYM_LINK) {
		object = object->value.sym_link;
	}
	Device *device = object->value.device;
	storage_generate_request(
		device->dm_ext, (direction == TRANSFER_IN) ? 0 : 1, buf, position,
		count, (StorageRequest **)handle);

	return TRANSFER_OK;
}

TransferResult storage_transfer(
	Object *object, ObjectHandle *obj_handle, TransferDirection direction,
	uint8_t *buf, uint32_t position, size_t count) {
	while (object->attr->type == OBJECT_TYPE_SYM_LINK) {
		object = object->value.sym_link;
	}
	Device *device = object->value.device;

	StorageRequest *request;
	StorageDevice  *storage_device = device->dm_ext;

	thread_set_status(TASK_INTERRUPTIBLE);
	wait_queue_add(&storage_device->wq);

	DriverResult result = storage_generate_request(
		storage_device, (direction == TRANSFER_IN) ? 0 : 1, buf, position,
		count, &request);
	if (result != DRIVER_RESULT_OK) {
		wait_queue_del(&storage_device->wq);
		return TRANSFER_ERROR_FAILED;
	}

	thread_wait();
	while (!request->is_finished) {
		thread_set_status(TASK_INTERRUPTIBLE);
		wait_queue_add(&storage_device->wq);
		thread_wait();
	}

	return TRANSFER_OK;
}

TransferResult storage_is_transfer_done(
	Object *object, void **handle, bool *done) {
	if (object->attr->type != OBJECT_TYPE_DEVICE) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	Device *device = object->value.device;

	if (device->device_driver->type != DEVICE_TYPE_STORAGE) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	StorageDevice *storage_device = device->dm_ext;

	StorageRequest *req = *handle;
	if (req->storage_device != storage_device || done == NULL) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	*done = req->is_finished;

	return TRANSFER_OK;
}