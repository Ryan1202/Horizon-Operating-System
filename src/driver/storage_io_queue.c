#include "types.h"
#include <driver/storage_dm.h>
#include <driver/storage_io_queue.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

bool storage_try_merge_request(
	StorageRequest *new_request, StorageRequest *request) {
	if (new_request->rw == request->rw) {
		if (new_request->position + new_request->count >= request->position &&
			new_request->position <= request->position + request->count) {
			if (request->next_merged_request != NULL) {
				// 如果是被合并过的请求，就继续合并
				StorageRequest *last_request = request->next_merged_request;
				while (last_request) {
					last_request = last_request->next_merged_request;
				}

				request->position =
					MIN(request->position, new_request->position);
				request->count = MAX(new_request->position + new_request->count,
									 request->position + request->count) -
								 request->position;

				last_request->next_merged_request = new_request;
			} else {
				// 如果未合并过，就新申请一个请求替换掉
				StorageRequest *req = kmalloc(sizeof(StorageRequest));
				req->buf = NULL; // 缓冲区先不申请，等到真正提交时再申请
				req->position = MIN(new_request->position, request->position);
				req->count	  = MAX(new_request->position + new_request->count,
									request->position + request->count) -
							 req->position;
				req->rw				= new_request->rw;
				req->is_finished	= 0;
				req->storage_device = new_request->storage_device;

				req->next_merged_request	 = request;
				request->next_merged_request = new_request;

				list_add_before(&req->list, &request->list);
				list_del(&request->list);
			}
			return true;
		}
	}
	return false;
}

void storage_add_request(
	StorageDevice *storage_device, StorageRequest *request) {
	StorageRequest *req;
	request->storage_device = storage_device;
	request->is_finished	= 0;
	list_for_each_owner (req, &storage_device->io_queue_lh, list) {
		if (storage_try_merge_request(request, req)) { return; }
		if (req->position > request->position) {
			list_add_before(&request->list, &req->list);
			return;
		}
	}
	list_add_tail(&request->list, &storage_device->io_queue_lh);
}

void storage_periodic_task(void *arg) {
	StorageDevice *storage_device = (StorageDevice *)arg;

	if (!storage_device->ops->is_busy(storage_device) &&
		!list_empty(&storage_device->io_queue_lh)) {
		StorageRequest *request = list_first_owner(
			&storage_device->io_queue_lh, StorageRequest, list);
		storage_submit_request(request);
	}
}

void storage_finish_request(StorageRequest *storage_request) {
	storage_request->is_finished = true;
	StorageRequest *req			 = storage_request->next_merged_request;
	if (req) {
		while (req) {
			req->is_finished = true;
			req				 = req->next_merged_request;
		}
	}
}

void storage_submit_request(StorageRequest *request) {
	StorageDevice *storage_device = request->storage_device;
	if (request->buf == NULL && request->next_merged_request != NULL) {
		request->buf = kmalloc(request->count * storage_device->block_size);
	}
	if (request->rw) {
		storage_device->ops->submit_write_request(storage_device, request);
	} else {
		storage_device->ops->submit_read_request(storage_device, request);
	}
	list_del(&request->list);
}

void storage_solve_read_request(StorageRequest *request) {
	StorageRequest *req = request->next_merged_request;
	if (req) {
		uint32_t start = request->position;
		while (req) {
			uint32_t offset =
				(req->position - start) * request->storage_device->block_size;
			memcpy(
				req->buf, request->real_buf + offset,
				req->count * req->storage_device->block_size);
			req = req->next_merged_request;
		}
	} else if (request->buf != request->real_buf) {
		memcpy(
			request->buf, request->real_buf,
			request->count * request->storage_device->block_size);
	}
}

void storage_solve_write_request(StorageRequest *request) {
	StorageRequest *req = request->next_merged_request;
	if (req) {
		uint32_t start = request->position;
		while (req) {
			uint32_t offset =
				(req->position - start) * request->storage_device->block_size;
			// 写入需要分先后，由于合并请求时已经按顺序排列了，直接覆盖就好
			memcpy(
				request->real_buf + offset, req->buf,
				req->count * req->storage_device->block_size);
			req = req->next_merged_request;
		}
	} else if (request->buf != request->real_buf) {
		memcpy(
			request->real_buf, request->buf,
			request->count * request->storage_device->block_size);
	}
}
