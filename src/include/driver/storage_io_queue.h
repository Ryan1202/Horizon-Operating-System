#ifndef _STORAGE_IO_QUEUE_H
#define _STORAGE_IO_QUEUE_H

#include "driver/storage_dm.h"
#include "kernel/list.h"
#include "stdint.h"
#include "types.h"

typedef struct StorageRequest {
	StorageDevice *storage_device;

	list_t	 list;
	bool	 rw;
	uint8_t *buf;
	uint32_t offset;
	uint64_t position;
	uint32_t count;
	bool	 is_finished;
} StorageRequest;

void storage_add_request(
	StorageDevice *storage_device, StorageRequest *request);
void storage_submit_request(StorageRequest *request);
void storage_finish_request(StorageRequest *storage_request);

#endif