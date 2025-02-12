#ifndef _TRANSFER_H
#define _TRANSFER_H

#include "result.h"
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
	uint32_t position, size_t count, void **handle);

typedef TransferResult (*StreamTransfer)(
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
} TransferIn;

typedef struct TransferOut {
	TransferType   type;
	IsTransferDone is_transfer_done;
	union {
		BlockTransfer  block;
		StreamTransfer stream;
	};
} TransferOut;

#define TRANSFER_IN_BLOCK(object)	   ((object)->in.block)
#define TRANSFER_IN_STREAM(object)	   ((object)->in.stream)
#define TRANSFER_IN_INTTERRUPT(object) ((object)->in.interrupt)

#define TRANSFER_IN_IS_DONE(object) ((object)->in.is_transfer_done)

#define TRANSFER_OUT_BLOCK(object)		((object)->in.block)
#define TRANSFER_OUT_STREAM(object)		((object)->in.stream)
#define TRANSFER_OUT_INTTERRUPT(object) ((object)->in.interrupt)

#define TRANSFER_OUT_IS_DONE(object) ((object)->in.is_transfer_done)

#endif