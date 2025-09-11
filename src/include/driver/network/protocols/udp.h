#ifndef _UDP_H
#define _UDP_H

#include "driver/network/buffer.h"
#include "driver/network/protocols/ipv4/ipv4.h"
#include <stdint.h>

#define UDP_PORT_DHCP_SERVER 67
#define UDP_PORT_DHCP_CLIENT 68

typedef struct {
	uint16_t src_port; // 源端口
	uint16_t dst_port; // 目的端口
	uint16_t length;   // 数据长度
	uint16_t checksum; // 校验和
} __attribute__((packed)) UdpHeader;

struct NetworkConnection;
void udp_register(struct NetworkConnection *conn);
void udp_wrap(
	struct NetworkConnection *conn, uint16_t src_port, uint16_t dst_port);
void udp_set_callback(
	struct NetworkConnection *conn,
	void (*callback)(struct NetworkConnection *conn, NetBuffer *buffer));
void		   udp_bind(struct NetworkConnection *conn, uint16_t port);
void		   udp_unbind(struct NetworkConnection *conn);
ProtocolResult udp_recv(NetBuffer *net_buffer, Ipv4Header *ipv4_header);

#endif