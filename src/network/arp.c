#include <kernel/driver.h>
#include <network/arp.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/network.h>

LIST_HEAD(arp_cache_lh);

uint8_t *ip2mac(netc_t *netc, uint8_t *ip) {
	arp_cache_t *cur, *next;
	arp_cache_t *cache;
	list_for_each_owner_safe (cur, next, &arp_cache_lh, list) {
		if (memcmp(cur->ip, ip, 4) == 0) {
			if (cur->mac[0] != 0) {
				return cur->mac;
			} else {
				wait_queue_add(&cur->wqm, 0);
				thread_block(TASK_BLOCKED);
			}
		}
	}

	cache = kmalloc(sizeof(arp_cache_t));
	memcpy(cache->ip, ip, 4);
	wait_queue_init(&cache->wqm);
	list_add_tail(&cache->list, &arp_cache_lh);
	send_arp(netc, ip, ARP_REQUEST);
	if (cache->mac[0] == 0) {
		wait_queue_add(&cur->wqm, 0);
		thread_block(TASK_BLOCKED);
	}
	return cache->mac;
}

void send_arp(netc_t *netc, uint8_t *dst_ip, uint16_t opcode) {
	arp_pack_t pack;

	pack.hwtype = HOST2BE_WORD(ARP_HWTYPE_ETH);
	pack.pttype = HOST2BE_WORD(ARP_PTTYPE_IPV4);
	pack.hwlen	= 6;
	pack.ptlen	= 4;
	pack.opcode = HOST2BE_WORD(opcode);

	memcpy(pack.src_hw_addr, netc->net_dev->info->mac, 6);
	ipv4_get_ip(netc, pack.src_ip);
	memcpy(pack.dst_ip, dst_ip, 4);
	memcpy(pack.dst_hw_addr, netc->dst_mac, 6);

	netc->net_dev->net_write(netc, (uint8_t *)&pack, sizeof(arp_pack_t));
}

void arp_read(uint8_t *buf, uint16_t offset, uint16_t length) {
	arp_pack_t	*arp;
	arp_cache_t *cur, *next;

	arp			= (arp_pack_t *)(buf + offset);
	arp->hwtype = BE2HOST_WORD(arp->hwtype);
	arp->pttype = BE2HOST_WORD(arp->pttype);
	arp->opcode = BE2HOST_WORD(arp->opcode);

	if (arp->opcode == ARP_REPLY) {
		list_for_each_owner_safe (cur, next, &arp_cache_lh, list) {
			if (memcmp(cur->ip, arp->src_ip, 4) == 0) {
				if (cur->mac[0] == 0) {
					memcpy(cur->mac, arp->src_hw_addr, 6);
					wait_queue_wakeup_all(&cur->wqm);
				} else {
					memcpy(cur->mac, arp->src_hw_addr, 6);
				}
				return;
			}
		}
	}
	return;
}
