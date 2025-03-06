#ifndef _TRANSFER_H
#define _TRANSFER_H

#include "stdint.h"
#include <types.h>

typedef enum {
	TRANSFER_OK,
	TRANSFER_ERROR_NOT_IMPLEMENTED,
	TRANSFER_ERROR_NOT_SUPPORTED,
	TRANSFER_ERROR_INVALID_PARAMETER,
	TRANSFER_ERROR_NO_MEMORY,
	TRANSFER_ERROR_OTHER,
} TransferResult;

typedef enum {
	TRANSFER_IN,
	TRANSFER_OUT,
} TransferDirection;

struct Object;
typedef TransferResult (*BlockTransfer)(
	struct Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count);
typedef TransferResult (*BlockTransferAsync)(
	struct Object *object, TransferDirection direction, uint8_t *buf,
	uint32_t position, size_t count, void **handle);

typedef TransferResult (*StreamTransfer)(
	struct Object *object, TransferDirection direction, uint8_t *buf,
	size_t size);
typedef TransferResult (*StreamTransferAsync)(
	struct Object *object, TransferDirection direction, uint8_t *buf,
	size_t size, void **handle);

typedef void (*InterruptTransferCallbackSingle)(
	struct Object *object, void *private_data, size_t data);
typedef void (*InterruptTransferCallbackMultiple)(
	struct Object *object, void *private_data, void *data, size_t size);
typedef TransferResult (*InterruptTransfer)(
	struct Object *object, InterruptTransferCallbackSingle callback_single,
	InterruptTransferCallbackMultiple callback_multiple);

typedef TransferResult (*IsTransferDone)(
	struct Object *object, void **handle, bool *done);

typedef enum TransferType {
	TRANSFER_TYPE_NONE,
	TRANSFER_TYPE_BLOCK,
	TRANSFER_TYPE_STREAM,
	TRANSFER_TYPE_INTERRUPT,
} TransferType;

typedef struct TransferIn {
	TransferType   type;
	IsTransferDone is_transfer_done;
	union {
		BlockTransfer	  block;
		StreamTransfer	  stream;
		InterruptTransfer interrupt;
	};
	union {
		BlockTransferAsync	block_async;
		StreamTransferAsync stream_async;
	};
} TransferIn;

typedef struct TransferOut {
	TransferType   type;
	IsTransferDone is_transfer_done;
	union {
		BlockTransfer  block;
		StreamTransfer stream;
	};
	union {
		BlockTransferAsync	block_async;
		StreamTransferAsync stream_async;
	};
} TransferOut;

#define TRANSFER_IN_BLOCK(object, ...) \
	((object)->in.block(object, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_BLOCK_ASYNC(object, ...) \
	((object)->in.block_async(object, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_STREAM(object, ...) \
	((object)->in.stream(object, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_STREAM_ASYNC(object, ...) \
	((object)->in.stream_async(object, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_INTTERRUPT(object, ...) \
	((object)->in.interrupt(object, __VA_ARGS__))

#define TRANSFER_IN_IS_DONE(object, ...) \
	((object)->in.is_transfer_done(object, __VA_ARGS__))

#define TRANSFER_OUT_BLOCK(object, ...) \
	((object)->in.block(object, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_BLOCK_ASYNC(object, ...) \
	((object)->in.block_async(object, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_STREAM(object, ...) \
	((object)->in.stream(object, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_STREAM_ASYNC(object, ...) \
	((object)->in.stream_async(object, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_INTTERRUPT(object, ...) \
	((object)->in.interrupt(object, __VA_ARGS__))

#define TRANSFER_OUT_IS_DONE(object, ...) \
	((object)->in.is_transfer_done(object, __VA_ARGS__))

#endif