#ifndef _NET_PROTOCOLS_H
#define _NET_PROTOCOLS_H

#include <result.h>

typedef enum {
	PROTO_OK,
	PROTO_DROP,
	PROTO_ERROR_UNSUPPORT,
	PROTO_ERROR_EXCEED_MAX_SIZE,
	PROTO_ERROR_NULL_PTR,
	PROTO_ERROR_CHECKSUM,
	PROTO_ERROR_CONNECT_FAILED,
	PROTO_ERROR_SEND_FAILED,
	PROTO_ERROR_REBIND,
	PROTO_ERROR_PORT_CONFLICT,
	PROTO_ERROR_CANNOT_FIND,
	PROTO_ERROR_NO_SPACE_LEFT,
	PROTO_ERROR_CANNOT_FIND_ROUTE,
	PROTO_ERROR_NOT_CONNECTED,
	PROTO_ERROR_NET_UNREACHABLE,
	PROTO_ERROR_HOST_UNREACHABLE,
	PROTO_ERROR_PORT_UNREACHABLE,
	PROTO_ERROR_OTHER,
} ProtocolResult;

struct NetworkDevice;
struct NetBuffer;
typedef ProtocolResult (*ProtocolReplyCallback)(
	struct NetworkDevice *device, struct NetBuffer *net_buffer);
ProtocolResult protocol_reply(
	struct NetworkDevice *device, struct NetBuffer *net_buffer,
	ProtocolReplyCallback *stack, int stack_size);

#endif