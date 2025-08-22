#ifndef _ARP_H
#define _ARP_H

#include "driver/network/conn.h"
#include "driver/network/protocols/protocols.h"
#include <stdint.h>

#define ARP_HTYPE_ETH 0x0001

#define ARP_TYPE 0x0806

#define ARP_OP_REQUEST 0x0001
#define ARP_OP_REPLY   0x0002

typedef struct ArpHeader {
	uint16_t htype;
	uint16_t ptype;
	uint8_t	 hlen;
	uint8_t	 plen;
	uint16_t opcode;
} __attribute__((packed)) ArpHeader;

extern NeighbourProtoOps arp_proto_ops;

ProtocolResult arp_recv(NetworkDevice *device, NetBuffer *net_buffer);
void		   arp_send_request(NeighbourEntry *entry, void *arg);
void		   arp_announce(NetworkDevice *device, uint8_t *ip_addr);

#endif