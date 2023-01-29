#ifndef ARP_H
#define ARP_H

#include "../stdint.h"
#include <kernel/driver.h>

#define ARP_HWTYPE_ETH 0x0001

typedef struct {
	uint16_t hwtype, pttype;
	uint8_t	 hwlen, ptlen;
	uint16_t opcode;
	uint8_t	 src_hw_addr[6];
	uint8_t	 src_ip[4];
	uint8_t	 dst_hw_addr[6];
	uint8_t	 dst_ip[4];
} __attribute__((packed)) arp_t;

void send_arp(device_t *device, arp_t *pack);

#endif