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
struct ObjectHandle;
typedef TransferResult (*BlockTransfer)(
	struct Object *object, struct ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, uint32_t position, size_t count);
typedef TransferResult (*BlockTransferAsync)(
	struct Object *object, struct ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, uint32_t position, size_t count,
	void **handle);

typedef TransferResult (*StreamTransfer)(
	struct Object *object, struct ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, size_t size);
typedef TransferResult (*StreamTransferAsync)(
	struct Object *object, struct ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, size_t size, void **handle);

typedef void (*InterruptTransferCallbackSingle)(
	struct Object *object, struct ObjectHandle *obj_handle, void *private_data,
	size_t data);
typedef void (*InterruptTransferCallbackMultiple)(
	struct Object *object, struct ObjectHandle *obj_handle, void *private_data,
	void *data, size_t size);
typedef TransferResult (*InterruptTransfer)(
	struct Object *object, struct ObjectHandle *obj_handle,
	InterruptTransferCallbackSingle	  callback_single,
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

#define TRANSFER_IN_BLOCK(object, handle, ...) \
	((object)->in.block(object, handle, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_BLOCK_ASYNC(object, handle, ...) \
	((object)->in.block_async(object, handle, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_STREAM(object, handle, ...) \
	((object)->in.stream(object, handle, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_STREAM_ASYNC(object, handle, ...) \
	((object)->in.stream_async(object, handle, TRANSFER_IN, __VA_ARGS__))
#define TRANSFER_IN_INTTERRUPT(object, handle, ...) \
	((object)->in.interrupt(object, handle, __VA_ARGS__))

#define TRANSFER_IN_IS_DONE(object, handle, ...) \
	((object)->in.is_transfer_done(object, handle, __VA_ARGS__))

#define TRANSFER_OUT_BLOCK(object, handle, ...) \
	((object)->in.block(object, handle, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_BLOCK_ASYNC(object, handle, ...) \
	((object)->in.block_async(object, handle, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_STREAM(object, handle, ...) \
	((object)->in.stream(object, handle, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_STREAM_ASYNC(object, handle, ...) \
	((object)->in.stream_async(object, handle, TRANSFER_OUT, __VA_ARGS__))
#define TRANSFER_OUT_INTTERRUPT(object, handle, ...) \
	((object)->in.interrupt(object, handle, __VA_ARGS__))

#define TRANSFER_OUT_IS_DONE(object, handle, ...) \
	((object)->in.is_transfer_done(object, handle, __VA_ARGS__))

#endif