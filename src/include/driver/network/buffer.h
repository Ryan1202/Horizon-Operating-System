#ifndef _NET_BUFFER_H
#define _NET_BUFFER_H

#include "kernel/list.h"
#include "protocols/protocols.h"
#include <stdint.h>

#define NET_BUF_RESV_HEAD(conn, n) \
	(conn)->buffer->head += (n);   \
	(conn)->buffer->data += (n);   \
	(conn)->buffer->tail += (n)

typedef struct NetBuffer {
	list_t list;

	void	*ptr;  // 缓冲区的地址
	uint16_t size; // 缓冲区的大小

	void *head; // 包头的起始地址
	void *data; // 数据的起始地址
	void *tail; // 数据的结束地址，也是包尾的起始地址
} NetBuffer;

NetBuffer *net_buffer_create(uint16_t size);
void	   net_buffer_init(
		  NetBuffer *buffer, uint16_t size, uint16_t head, uint16_t tail);

struct NetworkConnection;
ProtocolResult net_buffer_data_alloc(NetBuffer *buffer, uint16_t size);
void		   net_buffer_reset(NetBuffer *buffer);
void		   conn_header_alloc(struct NetworkConnection *conn, uint16_t size);
void		   conn_header_free(struct NetworkConnection *conn, uint16_t size);
void		   conn_tail_alloc(struct NetworkConnection *conn, uint16_t size);
void		   conn_tail_free(struct NetworkConnection *conn, uint16_t size);
ProtocolResult conn_put(struct NetworkConnection *conn, uint16_t size);

#endif