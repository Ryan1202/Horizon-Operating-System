#ifndef _TRANSFER_H
#define _TRANSFER_H

#include "kernel/driver.h"
#include "stdint.h"
#include "types.h"

struct Device;
typedef DriverResult (*BlockTransferIn)(
	struct Device *device, uint8_t *buf, offset_t offset, size_t size);
typedef DriverResult (*BlockTransferOut)(
	struct Device *device, uint8_t *buf, offset_t offset, size_t size);

typedef DriverResult (*StreamTransferIn)(
	struct Device *device, uint8_t *buf, size_t size);
typedef DriverResult (*StreamTransferOut)(
	struct Device *device, uint8_t *buf, size_t size);

typedef void (*InterruptTransferCallbackSingle)(
	struct Device *device, void *private_data, size_t data);
typedef void (*InterruptTransferCallbackMultiple)(
	struct Device *device, void *private_data, void *data, size_t size);
typedef DriverResult (*InterruptTransferIn)(
	struct Device *device, InterruptTransferCallbackSingle callback_single,
	InterruptTransferCallbackMultiple callback_multiple);

typedef enum TransferType {
	TRANSFER_TYPE_NONE,
	TRANSFER_TYPE_BLOCK,
	TRANSFER_TYPE_STREAM,
	TRANSFER_TYPE_INTERRUPT,
} TransferType;

typedef struct Transfer {
	TransferType type_in;
	union {
		BlockTransferIn		block;
		StreamTransferIn	stream;
		InterruptTransferIn interrupt;
	} in;

	TransferType type_out;
	union {
		BlockTransferOut  block;
		StreamTransferOut stream;
	} out;
} Transfer;

#endif