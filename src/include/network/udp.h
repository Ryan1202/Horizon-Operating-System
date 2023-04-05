#ifndef UDP_H
#define UDP_H

#include "../stdint.h"
#include "network.h"

typedef struct {
	uint16_t SourcePort;
	uint16_t DestinationPort;
	uint16_t Length;
	uint16_t Checksum;
} __attribute__((packed)) udp_head_t;

typedef struct {
	struct netc_s *netc;
	uint16_t	   src_port, dst_port;
	uint8_t		   dst_ip[4];
	list_t		   list;
} udp_conn_t;

#define PROTOCOL_UDP 0x11

int	 udp_bind(netc_t *netc, uint8_t ip[4], uint16_t src_port, uint16_t dst_port);
int	 udp_unbind(netc_t *netc);
void udp_send(netc_t *netc, uint16_t *data, uint16_t datalen);
void udp_read(uint8_t *buf, uint16_t offset, uint16_t length);

#endif