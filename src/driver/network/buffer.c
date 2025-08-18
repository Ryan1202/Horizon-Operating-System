#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <driver/network/protocols/protocols.h>
#include <kernel/memory.h>
#include <stdint.h>

NetBuffer *net_buffer_create(uint16_t size) {
	NetBuffer *buffer = kmalloc(sizeof(NetBuffer));
	if (buffer == NULL) { return NULL; }
	buffer->ptr = kmalloc(size);
	if (buffer->ptr == NULL) {
		kfree(buffer);
		return NULL;
	}
	buffer->size = size;
	return buffer;
}

void net_buffer_init(
	NetBuffer *buffer, uint16_t size, uint16_t head, uint16_t tail) {
	buffer->size = size;

	buffer->head = buffer->ptr + head;
	buffer->data = buffer->ptr + head;
	buffer->tail = buffer->ptr + tail;
}

void net_buffer_reset(NetBuffer *buffer) {
	buffer->head = buffer->data;
	buffer->tail = buffer->data;
}

void conn_header_alloc(NetworkConnection *conn, uint16_t size) {
	conn->buffer->head -= size;
}

void conn_header_free(NetworkConnection *conn, uint16_t size) {
	conn->buffer->head += size;
}

void conn_tail_alloc(NetworkConnection *conn, uint16_t size) {
	conn->buffer->tail += size;
}

void conn_tail_free(NetworkConnection *conn, uint16_t size) {
	conn->buffer->tail -= size;
}

ProtocolResult net_buffer_data_alloc(NetBuffer *buffer, uint16_t size) {
	if (buffer->tail + size > buffer->ptr + buffer->size) {
		// 如果数据超过了缓冲区的大小，则不进行写入
		return PROTO_ERROR_EXCEED_MAX_SIZE;
	}
	buffer->tail += size;
	return PROTO_OK;
}

ProtocolResult conn_put(NetworkConnection *conn, uint16_t size) {
	NetBuffer *buffer = conn->buffer;
	if (buffer->tail + size > buffer->ptr + buffer->size) {
		// 如果数据超过了缓冲区的大小，则不进行写入
		return PROTO_ERROR_EXCEED_MAX_SIZE;
	}
	buffer->tail += size;

	return PROTO_OK;
}
