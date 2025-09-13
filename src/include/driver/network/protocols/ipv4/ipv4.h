#ifndef _IPV4_H
#define _IPV4_H

#include "driver/network/neighbour.h"
#include "kernel/list.h"
#include <driver/network/protocols/protocols.h>
#include <stdint.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

#define IPv4_DEFAULT_MSS 536

#define CONN_LOCAL_IP(conn)	 conn->ipv4.conn_info.local.ip
#define CONN_REMOTE_IP(conn) conn->ipv4.conn_info.remote.ip

typedef struct Ipv4Header {
	uint8_t	 ver_len;			// Version and Internet Header Length
	uint8_t	 tos;				// Type of Service
	uint16_t total_len;			// Total Length
	uint16_t id;				// Identification
	uint16_t flags_frag_offset; // Flags and Fragment Offset
	uint8_t	 ttl;				// Time to Live
	uint8_t	 protocol;			// Protocol
	uint16_t checksum;			// Header Checksum
	uint8_t	 src_ip[4];			// Source IP Address
	uint8_t	 dst_ip[4];			// Destination IP Address
	uint8_t	 options[0];		// Options
} __attribute__((packed)) Ipv4Header;

typedef struct Ipv4Endpoint {
	uint8_t	 ip[4]; // IP地址
	uint16_t port;	// 端口号
} Ipv4Endpoint;

typedef struct Ipv4ConnInfo {
	Ipv4Endpoint local;	 // 本地IP和端口
	Ipv4Endpoint remote; // 远程IP和端口
	list_t		 list;
} Ipv4ConnInfo;

extern const uint32_t ipv4_broadcast_addr;
extern const uint32_t ipv4_null_addr;

struct NetworkConnection;
struct NetBuffer;
void		   ipv4_register(struct NetworkConnection *conn, uint8_t *ip_addr);
void		   ipv4_enable_fragment(struct NetworkConnection *conn);
ProtocolResult ipv4_wrap(
	struct NetworkConnection *conn, uint16_t protocol, uint8_t *dst_ip,
	uint8_t ttl);
void		   ipv4_rewrap(struct NetworkConnection *conn);
ProtocolResult ipv4_recv(
	struct NetworkDevice *device, struct NetBuffer *net_buffer);
NeighbourKey ipv4_hash(uint8_t ip[4]);
void		 ipv4_neigh_update(
			NetworkDevice *device, struct NetBuffer *buffer, uint8_t *mac, int hlen);
ProtocolResult ipv4_lookup_mac(
	NetworkDevice *device, uint8_t ip[4], uint8_t mac[8]);
uint16_t ipv4_get_packet_length(Ipv4Header *ipv4_header);

#endif