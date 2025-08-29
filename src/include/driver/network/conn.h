#ifndef _NET_CONN_H
#define _NET_CONN_H

#include "buffer.h"
#include "driver/network/ethernet/ethernet.h"
#include "driver/network/network_dm.h"
#include "driver/network/protocols/ipv4.h"
#include "kernel/list.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include "kernel/wait_queue.h"
#include "objects/handle.h"
#include "objects/object.h"
#include <stdint.h>

#define NET_CONN_MAX_PROTOCOLS 8

#define CONN_PACKET_SIZE(conn)	((conn)->buffer->tail - (conn)->buffer->head)
#define CONN_CONTENT_SIZE(conn) ((conn)->buffer->tail - (conn)->buffer->data)

#define conn_buffer(conn) ((conn)->buffer)

struct NetworkConnection;
typedef struct NetProtocol {
	uint16_t head_size;
	uint16_t tail_size;
} NetProtocol;

struct Tcp;
typedef struct NetworkConnection {
	Object		  *object;
	ObjectHandle  *handle;
	NetworkDevice *net_device;

	NetBuffer *buffer;

	struct task_s *thread;

	// physical layer protocol
	enum {
		PHY_PROTO_NONE,
		PHY_PROTO_ETHERNET,
	} phy_protocol;
	union {
		struct {
			uint8_t mac[6];
		} ethernet;
	};

	// data link layer protocol
	enum {
		DL_PROTO_NONE,
		DL_PROTO_ARP,
	} dl_protocol;
	union {};

	// network layer protocol
	enum {
		NET_PROTO_NONE,
		NET_PROTO_IPV4,
	} net_protocol;
	union {
		struct {
			uint16_t id;
			struct {
				uint16_t enable_fragment : 1;
				uint16_t last_fragment	 : 1;
				uint16_t frag_offset	 : 13;
			} fragment;
			Ipv4Header		   *header;
			struct Ipv4ConnInfo conn_info; // 连接信息
		} ipv4;
	};

	// transport layer protocol
	enum {
		TRANS_PROTO_NONE,
		TRANS_PROTO_TCP,
		TRANS_PROTO_UDP,
	} trans_protocol;
	union {
		struct {
			void *private_data;
			void (*callback)(
				struct NetworkConnection *conn, NetBuffer *net_buffer);
		} udp;
		struct {
			struct Tcp *info;
		} tcp;
	};
	spinlock_t recv_lock;
	list_t	   recv_lh;
} NetworkConnection;

typedef enum ProtocolLevel {
	PROTO_LEVEL_TRANSPORT,
	PROTO_LEVEL_NETWORK,
	PROTO_LEVEL_DATA_LINK,
	PROTO_LEVEL_PHYSICAL
} ProtocolLevel;

NetworkConnection *net_create_conn(Object *object);
void			   net_destroy_conn(NetworkConnection *conn);
ProtocolResult	   conn_wrap(NetworkConnection *conn, ProtocolLevel level);

#endif