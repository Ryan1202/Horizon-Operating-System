#ifndef _ARP_H
#define _ARP_H

#include "../conn.h"
#include "protocols.h"
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

ProtocolResult arp_wrap(
	NetworkConnection *conn, uint8_t *dst_haddr, uint8_t *dst_paddr,
	uint16_t opcode);

#endif