#ifndef ARP_H
#define ARP_H

#include "../stdint.h"
#include <kernel/driver.h>
#include <kernel/list.h>
#include <network/network.h>

#define ARP_HWTYPE_ETH 0x0001

#define ARP_PTTYPE_IPV4 0x0800

#define ARP_REQUEST 1
#define ARP_REPLY	2

typedef struct {
	uint16_t hwtype, pttype;
	uint8_t	 hwlen, ptlen;
	uint16_t opcode;
	uint8_t	 src_hw_addr[6];
	uint8_t	 src_ip[4];
	uint8_t	 dst_hw_addr[6];
	uint8_t	 dst_ip[4];
} __attribute__((packed)) arp_pack_t;

typedef struct {
	uint8_t ip[4];
	uint8_t mac[6];
	list_t	list;

	wait_queue_manager_t wqm;
} arp_cache_t;

uint8_t *ip2mac(netc_t *netc, uint8_t *ip);
void	 send_arp(netc_t *netc, uint8_t *dst_ip, uint16_t opcode);
void	 arp_read(uint8_t *buf, uint16_t offset, uint16_t length);

#endif