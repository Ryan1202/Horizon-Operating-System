#ifndef UDP_H
#define UDP_H

#include "../stdint.h"

typedef struct {
	uint16_t SourcePort;
	uint16_t DestinationPort;
	uint16_t Length;
	uint16_t Checksum;
} __attribute__((packed)) udp_head_t;

#define PROTOCOL_UDP 0x11

void udp_send(netc_t *netc, uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port, uint16_t *data,
			  uint16_t datalen);
void udp_read(netc_t *netc, uint8_t *buf, uint16_t offset, uint16_t length);

#endif