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

void net_buffer_header_alloc(NetBuffer *buffer, uint16_t size) {
	buffer->head -= size;
}

void net_buffer_header_free(NetBuffer *buffer, uint16_t size) {
	buffer->head += size;
}

void net_buffer_tail_alloc(NetBuffer *buffer, uint16_t size) {
	buffer->tail += size;
}

void net_buffer_tail_free(NetBuffer *buffer, uint16_t size) {
	buffer->tail -= size;
}

ProtocolResult net_buffer_data_alloc(NetBuffer *buffer, uint16_t size) {
	if (buffer->tail + size > buffer->ptr + buffer->size) {
		// 如果数据超过了缓冲区的大小，则不进行写入
		return PROTO_ERROR_EXCEED_MAX_SIZE;
	}
	buffer->tail += size;
	return PROTO_OK;
}

ProtocolResult net_buffer_put(NetBuffer *buffer, uint16_t size) {
	if (buffer->tail + size > buffer->ptr + buffer->size) {
		// 如果数据超过了缓冲区的大小，则不进行写入
		return PROTO_ERROR_EXCEED_MAX_SIZE;
	}
	buffer->tail += size;

	return PROTO_OK;
}
