#include <kernel/driver.h>
#include <network/arp.h>
#include <network/network.h>

LIST_HEAD(arp_cache_lh);

uint8_t *ip2mac(device_t *device, uint8_t *src_mac, uint8_t *ip) {
	arp_cache_t *cur, *next;
	list_for_each_owner_safe (cur, next, &arp_cache_lh, list) {
		if (memcmp(cur->ip, ip, 4) == 0) { return cur->mac; }
	}
	send_arp(device, src_mac, ip, ARP_REQUEST);
	return NULL;
}

void send_arp(device_t *device, uint8_t *src_mac, uint8_t *dst_ip, uint16_t opcode) {
	int			  i;
	const uint8_t src_ip[] = {0, 0, 0, 0}, dst_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	arp_pack_t	  pack;

	pack.hwtype = HOST2BE_WORD(ARP_HWTYPE_ETH);
	pack.pttype = HOST2BE_WORD(ARP_PTTYPE_IPV4);
	pack.hwlen	= 6;
	pack.ptlen	= 6;
	pack.opcode = HOST2BE_WORD(opcode);

	memcpy(pack.src_hw_addr, src_mac, 6);

	if (device->type == DEV_ETH_NET) {
		eth_write(device, src_mac, (uint8_t *)dst_mac, ETH_TYPE_ARP, (uint8_t *)&pack, sizeof(arp_pack_t));
	}
}
