#ifndef _STORAGE_IO_QUEUE_H
#define _STORAGE_IO_QUEUE_H

#include "driver/storage/storage_dm.h"
#include "kernel/list.h"
#include "stdint.h"
#include "types.h"

typedef struct StorageRequest {
	StorageDevice *storage_device;

	list_t	 list;
	bool	 rw;
	uint8_t *buf;	   // 调用方传入的缓冲区
	uint8_t *real_buf; // 实际读写时使用的缓冲区
	uint64_t position;
	uint32_t count;
	bool	 is_finished;

	struct StorageRequest *next_merged_request;
} StorageRequest;

void storage_add_request(
	StorageDevice *storage_device, StorageRequest *request);
void storage_submit_request(StorageRequest *request);
void storage_finish_request(StorageRequest *storage_request);

void storage_solve_read_request(StorageRequest *request);
void storage_solve_write_request(StorageRequest *request);

#endif