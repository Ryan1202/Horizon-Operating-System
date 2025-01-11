#include <driver/storage_dm.h>
#include <driver/storage_io_queue.h>
#include <kernel/list.h>

bool storage_try_merge_request(
	StorageRequest *new_request, StorageRequest *request) {
	if (new_request->rw == request->rw) {
		if (new_request->position + new_request->count >= request->position &&
			new_request->position <= request->position + request->count) {
			new_request->count += request->count;
			return true;
		}
	}
	return false;
}

void storage_add_request(
	StorageDevice *storage_device, StorageRequest *request) {
	StorageRequest *req;
	request->storage_device = storage_device;
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
}

void storage_submit_request(StorageRequest *request) {
	StorageDevice *storage_device = request->storage_device;
	if (request->rw) {
		storage_device->ops->submit_write_request(storage_device, request);
	} else {
		storage_device->ops->submit_read_request(storage_device, request);
	}
	list_del(&request->list);
}