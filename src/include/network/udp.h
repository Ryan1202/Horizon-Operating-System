#ifndef UDP_H
#define UDP_H

#include "../stdint.h"

typedef struct {
	uint16_t SourcePort;
	uint16_t DestinationPort;
	uint16_t Length;
	uint16_t Checksum;
} udp_head_t;

#define PROTOCOL_UDP 0x11

void udp_send(device_t *device, uint8_t *src_mac, uint8_t *src_ip, uint8_t *dst_mac, uint8_t *dst_ip,
			  uint16_t src_port, uint16_t dst_port, uint16_t *data, uint16_t datalen);

#endif