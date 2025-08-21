/**
 * @file ipv4.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief IPv4
 *
 * Reference:
 * RFC 791: INTERNET PROTOCOL
 *
 */
#include <bits.h>
#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <driver/network/neighbour.h>
#include <driver/network/protocols/ipv4.h>
#include <driver/network/protocols/protocols.h>
#include <driver/network/protocols/udp.h>
#include <kernel/spinlock.h>
#include <random.h>
#include <stdint.h>
#include <string.h>

SPINLOCK(ipv4_id_lock);
uint16_t ipv4_id_counter = 0;

const uint32_t ipv4_broadcast_addr = 0xffffffff;
const uint32_t ipv4_null_addr	   = 0x00000000;

NeighbourKey ipv4_hash(uint8_t ip[4]) {
	uint32_t ip32 = *(uint32_t *)ip;
	return (ip32 ^ (ip32 >> 16)) % NEIGH_BUCKET_SIZE;
}

void ipv4_register(NetworkConnection *conn, uint8_t *ip_addr) {
	conn->net_protocol = NET_PROTO_IPV4;
	if (ip_addr) {
		memcpy(conn->ipv4.ip, ip_addr, 4);
	} else {
		memset(conn->ipv4.ip, 0, 4);
	}
	// 默认禁用分段
	conn->ipv4.fragment.enable_fragment = 0;
	conn->ipv4.fragment.last_fragment	= 0;
	conn->ipv4.fragment.frag_offset		= 0;
	conn->ipv4.id						= 0;
	NET_BUF_RESV_HEAD(conn, sizeof(Ipv4Header));
}

void ipv4_enable_fragment(NetworkConnection *conn) {
	conn->ipv4.fragment.enable_fragment = 1;
	conn->ipv4.fragment.last_fragment	= 0;
	conn->ipv4.fragment.frag_offset		= 0;

	spin_lock(&ipv4_id_lock);
	if (ipv4_id_counter == 0) ipv4_id_counter = rand() & 0xffff;
	conn->ipv4.id = ipv4_id_counter++;
	spin_unlock(&ipv4_id_lock);
}

void ipv4_checksum(Ipv4Header *header) {
	uint32_t  sum  = 0;
	uint16_t *data = (void *)header;
	uint8_t	  len  = sizeof(Ipv4Header);

	header->checksum = 0;

	for (size_t i = 0; i < len / 2; i++) {
		sum += data[i];
		if (sum > 0xFFFF) sum = (sum & 0xFFFF) + (sum >> 16);
	}
	header->checksum = ~((uint16_t)sum);
};

ProtocolResult ipv4_wrap(
	NetworkConnection *conn, uint16_t protocol, uint8_t *dst_ip, uint8_t ttl) {
	uint16_t size = CONN_CONTENT_SIZE(conn);

	net_buffer_header_alloc(conn_buffer(conn), sizeof(Ipv4Header));
	Ipv4Header *ipv4_header = (Ipv4Header *)conn_buffer(conn)->head;
	ipv4_header->ver_len	= (0x4 << 4) | (20 >> 2); // IPv4, 20字节
	ipv4_header->tos		= 0;					  // Type of Service
	ipv4_header->total_len	= HOST2BE_WORD(sizeof(Ipv4Header) + size);
	ipv4_header->id			= HOST2BE_WORD(conn->ipv4.id);

	ipv4_header->flags_frag_offset = HOST2BE_WORD(
		conn->ipv4.fragment.enable_fragment << 14 |
		conn->ipv4.fragment.last_fragment << 15 |
		conn->ipv4.fragment.frag_offset);

	ipv4_header->ttl	  = ttl;
	ipv4_header->protocol = protocol;
	ipv4_header->checksum = 0;
	memcpy(ipv4_header->src_ip, conn->ipv4.ip, 4);
	if (dst_ip) {
		memcpy(ipv4_header->dst_ip, dst_ip, 4);
	} else {
		*(uint32_t *)ipv4_header->dst_ip = 0xffffffff; // 广播地址
	}
	ipv4_checksum(ipv4_header);

	return PROTO_OK;
}

ProtocolResult ipv4_recv(NetBuffer *net_buffer) {
	Ipv4Header *ipv4_header = (Ipv4Header *)net_buffer->data;
	int			size		= net_buffer->tail - net_buffer->data;

	if (size < sizeof(Ipv4Header)) return PROTO_ERROR_UNSUPPORT;

	if ((ipv4_header->ver_len >> 4) != 0x4) return PROTO_ERROR_UNSUPPORT;

	if (ipv4_header->checksum != 0) {
		uint16_t checksum = ipv4_header->checksum;
		ipv4_checksum(ipv4_header);
		if (ipv4_header->checksum != checksum) return PROTO_ERROR_CHECKSUM;
	}

	uint16_t frag_offset = HOST2BE_WORD(ipv4_header->flags_frag_offset);
	bool	 mf = (frag_offset & 0x2000) != 0; // More Fragments，还有分片
	frag_offset &= 0x1FFF;
	if (frag_offset || mf) {
		// 处理分片
		return PROTO_ERROR_UNSUPPORT; // 暂不支持分片
	}

	net_buffer->data += sizeof(Ipv4Header);

	ProtocolResult result = PROTO_OK;
	switch (ipv4_header->protocol) {
	case IP_PROTO_UDP:
		result = udp_recv(net_buffer, ipv4_header);
		break;
	case IP_PROTO_TCP:
		break;
	case IP_PROTO_ICMP:
		break;
	default:
		result = PROTO_ERROR_UNSUPPORT;
	}

	return result;
}
