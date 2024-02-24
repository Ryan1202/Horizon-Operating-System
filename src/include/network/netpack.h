#ifndef NETPACK_H
#define NETPACK_H

#include "kernel/list.h"
#include "network/network.h"
#include "network/tcp.h"
#include "network/udp.h"
#include "stdint.h"

typedef struct net_rx_pack_s {
	list_t list;

	enum frame_type {
		ETH_FRAME,
	} type;
	uint8_t *src_ip_addr;
	uint8_t	 src_ip_len;
	uint32_t proto_start;
	uint32_t data_len;

	uint8_t *data;
	uint32_t len;
} net_rx_pack_t;

extern list_t net_rx_tcp_lh;

void net_process_pack(void *arg);
void net_rx_raw_pack(enum frame_type type, uint8_t *buf, uint32_t length);
void net_raw2tcp_pack(net_rx_pack_t *pack, uint8_t ip_len, uint8_t *p_addr, uint32_t start, uint32_t len);
void net_raw2udp_pack(net_rx_pack_t *pack, uint32_t start, uint32_t len);

#endif