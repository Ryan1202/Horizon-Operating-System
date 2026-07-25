/**
 * 默认的存储设备IO实现
 */
#include "kernel/device.h"
#include "kernel/driver.h"
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
	DEF_MRET(StorageRequest *, request_list)) {
	// 计算需要分割成几个请求
	uint32_t num_requests = DIV_ROUND_UP(count, device->max_block_per_request);
	uint32_t remaining_blocks = count;
	uint32_t current_position = position;

	StorageRequest *first_request = NULL;
	StorageRequest *last_request  = NULL;

	// uint32_t t, t0, t1, t2, t3;
	// 分割请求
	for (uint32_t i = 0; i < num_requests; i++) {
		// 计算当前分片的大小
		uint32_t current_count =
			MIN(remaining_blocks, device->max_block_per_request);

		// 创建新的请求
		StorageRequest *request = kzalloc(sizeof(StorageRequest));
		if (request == NULL || !completion_init(&request->completion)) {
			if (request != NULL) kfree(request);
			while (first_request != NULL) {
				StorageRequest *next = first_request->batch_next;
				completion_deinit(&first_request->completion);
				kfree(first_request);
				first_request = next;
			}
			return DRIVER_ERROR_OUT_OF_MEMORY;
		}

		// 设置新请求的参数
		request->position			 = current_position;
		request->count				 = current_count;
		request->rw					 = rw;
		request->is_finished		 = 0;
		request->storage_device		 = device;
		request->next_merged_request = NULL;
		request->batch_next           = NULL;

		// 分配或指向原始缓冲区中对应的部分
		request->buf =
			buf + i * device->max_block_per_request * device->block_size;

		// 更新剩余块和当前位置
		remaining_blocks -= current_count;
		current_position += current_count;
		if (last_request == NULL) {
			first_request = request;
		} else {
			last_request->batch_next = request;
		}
		last_request = request;
	}

	for (StorageRequest *request = first_request; request != NULL;
		 request = request->batch_next) {
		storage_add_request(device, request);
	}

	MRET(request_list) = first_request;
	return DRIVER_OK;
}

TransferResult storage_transfer_async(
	Object *object, ObjectHandle *obj_handle, TransferDirection direction,
	uint8_t *buf, uint32_t position, size_t count, void **handle) {
	while (object->attr->type == OBJECT_TYPE_SYM_LINK) {
		object = object->value.sym_link;
	}
	LogicalDevice *device = object->value.device.logical;
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
	LogicalDevice *device = object->value.device.logical;

	StorageRequest *requests;
	StorageDevice  *storage_device = device->dm_ext;

	DriverResult result = storage_generate_request(
		storage_device, (direction == TRANSFER_IN) ? 0 : 1, buf, position,
		count, &requests);
	if (result != DRIVER_OK) {
		return TRANSFER_ERROR_FAILED;
	}

	for (StorageRequest *request = requests; request != NULL;
		 request = request->batch_next) {
		completion_wait(&request->completion);
	}
	/*
	 * StorageRequest 的所有权仍由旧存储队列协议决定。完成只表示 I/O
	 * 已结束，不足以证明驱动和合并请求已经不再持有该指针；在该协议
	 * 被单独重构前，这里保持旧实现的生命周期，不主动回收请求。
	 */

	return TRANSFER_OK;
}

TransferResult storage_is_transfer_done(
	Object *object, void **handle, bool *done) {
	if (object->attr->type != OBJECT_TYPE_DEVICE) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	LogicalDevice *device = object->value.device.logical;

	if (device->type != DEVICE_TYPE_STORAGE) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	StorageDevice *storage_device = device->dm_ext;

	StorageRequest *req = *handle;
	if (req->storage_device != storage_device || done == NULL) {
		return TRANSFER_ERROR_INVALID_PARAMETER;
	}
	*done = true;
	for (; req != NULL; req = req->batch_next) {
		if (!req->is_finished) {
			*done = false;
			break;
		}
	}

	return TRANSFER_OK;
}
