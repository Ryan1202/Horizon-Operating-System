#include "kernel/block_cache.h"
#include "kernel/rwlock.h"
#include "kernel/spinlock.h"
#include "kernel/wait_queue.h"
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io_queue.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

PRIVATE void storage_modify_merged_request(
	StorageRequest *new_request, StorageRequest *request) {
	StorageRequest *last_request = request->next_merged_request;
	while (last_request->next_merged_request != NULL) {
		last_request = last_request->next_merged_request;
	}

	request->position = MIN(request->position, new_request->position);
	request->count	  = MAX(new_request->position + new_request->count,
							request->position + request->count) -
					 request->position;

	last_request->next_merged_request = new_request;
}

PRIVATE void storage_new_merge_request(
	StorageRequest *new_request, StorageRequest *request) {
	StorageRequest *req = kzalloc(sizeof(StorageRequest));
	req->buf			= NULL; // 缓冲区先不申请，等到真正提交时再申请
	req->position		= MIN(new_request->position, request->position);
	req->count			= MAX(new_request->position + new_request->count,
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

bool storage_try_merge_request(
	StorageRequest *new_request, StorageRequest *request, size_t max_count) {
	if (new_request->rw == request->rw) { // 读写类型相同
		if (new_request->count + request->count > max_count) return false;
		if (new_request->position + new_request->count < request->position &&
			new_request->position > request->position + request->count) {
			// 两个请求没有交集
			return false;
		}

		if (request->next_merged_request != NULL) {
			// 如果是被合并过的请求，就继续合并
			storage_modify_merged_request(new_request, request);
		} else { // 未合并过
			// 因为不能修改已有的请求，就新申请一个请求替换掉
			storage_new_merge_request(new_request, request);
		}
	}
	return false;
}

void storage_add_request(
	StorageDevice *storage_device, StorageRequest *request) {
	request->storage_device = storage_device;
	request->is_finished	= 0;

	spin_lock(&storage_device->queue_lock);

	if (list_empty(&storage_device->io_queue_lh) &&
		!storage_device->ops->is_busy(storage_device)) {
		storage_submit_request(request);
		spin_unlock(&storage_device->queue_lock);
		return;
	}
	spin_unlock(&storage_device->queue_lock);

	StorageRequest *req;
	spin_lock(&storage_device->queue_lock);
	list_for_each_owner (req, &storage_device->io_queue_lh, list) {
		if (storage_try_merge_request(
				request, req, storage_device->max_block_per_request)) {
			return;
		}
		if (req->position > request->position) {
			list_add_before(&request->list, &req->list);
			return;
		}
	}
	list_add_tail(&request->list, &storage_device->io_queue_lh);
	spin_unlock(&storage_device->queue_lock);
}

void storage_periodic_task(void *arg) {
	StorageDevice *storage_device = (StorageDevice *)arg;

	if (!storage_device->ops->is_busy(storage_device)) {
		spin_lock(&storage_device->queue_lock);
		if (!list_empty(&storage_device->io_queue_lh)) {
			StorageRequest *request = list_first_owner(
				&storage_device->io_queue_lh, StorageRequest, list);
			storage_submit_request(request);
		} else if (
			list_in_list(&storage_device->block_cache_lh) &&
			!list_empty(&storage_device->block_cache_lh)) {
			BlockCacheEntry *entry = list_first_owner(
				&storage_device->block_cache_lh, BlockCacheEntry, list);

			rwlock_read_lock(&entry->lock);

			entry->cache->write(
				entry, entry->cache->size, entry->cache->private_data);
			list_del(&entry->list);
			entry->dirty = false;

			rwlock_read_unlock(&entry->lock);
		}
		spin_unlock(&storage_device->queue_lock);
	}
}

// 需要修改storage_finish_request函数，支持分割请求的完成
void storage_finish_request(StorageRequest *storage_request) {
	storage_request->is_finished = true;

	// 处理合并请求的情况
	StorageRequest *req = storage_request->next_merged_request;
	if (req) {
		while (req) {
			req->is_finished = true;
			req				 = req->next_merged_request;
		}
	}
	wait_queue_wakeup_thread(
		&storage_request->storage_device->wq, storage_request->thread);
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
	if (list_in_list(&request->list)) list_del(&request->list); // TODO: bug
}

void storage_solve_read_request(StorageRequest *request) {
	// 处理合并请求的情况
	StorageRequest *req = request->next_merged_request;
	if (req) {
		uint32_t start = request->position;
		while (req) {
			uint32_t offset =
				(req->position - start) * request->storage_device->block_size;
			memcpy(
				req->buf, request->buf + offset,
				req->count * req->storage_device->block_size);
			req = req->next_merged_request;
		}
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
				request->buf + offset, req->buf,
				req->count * req->storage_device->block_size);
			req = req->next_merged_request;
		}
	}
}
