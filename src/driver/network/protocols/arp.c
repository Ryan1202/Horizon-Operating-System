/**
 * @file arp.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief ARP
 *
 * Reference:
 * RFC 826: An Ethernet Address Resolution Protocol
 *
 */
#include "driver/network/buffer.h"
#include "driver/network/ethernet/ethernet.h"
#include "driver/network/network_dm.h"
#include "kernel/list.h"
#include "kernel/spinlock.h"
#include <bits.h>
#include <driver/network/conn.h>
#include <driver/network/neighbour.h>
#include <driver/network/protocols/arp.h>
#include <driver/network/protocols/protocols.h>
#include <network/eth.h>
#include <stdint.h>
#include <string.h>

void arp_send_request(NeighbourEntry *entry, void *arg);

NeighbourProtoOps arp_proto_ops = {
	.request = arp_send_request,
};

ProtocolResult arp_recv(NetworkDevice *device, NetBuffer *net_buffer) {
	ArpHeader *arp_header = (ArpHeader *)net_buffer->data;

	void *p			= (void *)(arp_header + 1);
	void *src_haddr = p;
	void *src_paddr = p + arp_header->hlen;
	void *dst_haddr = p + arp_header->hlen + arp_header->plen;
	void *dst_paddr = p + arp_header->hlen * 2 + arp_header->plen;

	// 检查硬件类型
	uint16_t htype = BE2HOST_WORD(arp_header->htype);
	uint8_t	 haddr[8];
	uint8_t *mac_addr = device->ethernet->mac_addr;
	if (htype == ARP_HTYPE_ETH && device->type == NETWORK_TYPE_ETHERNET) {
		if (arp_header->hlen == ETH_IDENTIFIER_SIZE) {
			if (memcmp(dst_haddr, eth_broadcast_mac, arp_header->hlen) == 0) {
				// 处理广播地址
				p = mac_addr;
				goto next;
			} else if (memcmp(dst_haddr, mac_addr, arp_header->hlen) == 0) {
				// 处理本地地址
				p = dst_haddr;
				goto next;
			}
		}
	}
	return PROTO_ERROR_UNSUPPORT; // 不支持的硬件类型
next:
	memcpy(haddr, p, arp_header->hlen);

	bool		 merge_flag = false;
	NeighbourKey hash_key	= NEIGH_BUCKET_SIZE;

	// 检查协议类型
	uint8_t *paddr = NULL;
	uint16_t ptype = BE2HOST_WORD(arp_header->ptype);
	if (ptype == ETH_TYPE_IPV4 && arp_header->plen == 4) {
		hash_key = ipv4_hash(src_paddr) % NEIGH_BUCKET_SIZE;
		paddr	 = device->ipv4_addr;
	} else {
		return PROTO_ERROR_UNSUPPORT; // 不支持的协议类型
	}

	if (hash_key >= NEIGH_BUCKET_SIZE) { return PROTO_ERROR_UNSUPPORT; }

	NeighbourEntry *entry, *next;
	spin_lock(&neighbour_table.lock[hash_key]);
	list_for_each_owner_safe (
		entry, next, &neighbour_table.buckets[hash_key], list) {
		spin_lock(&entry->lock);
		if (arp_header->plen == entry->ip_length &&
			memcmp(entry->ip_addr, src_paddr, arp_header->plen) == 0) {
			merge_flag = true;
			break;
		}
		spin_unlock(&entry->lock);
	}
	spin_unlock(&neighbour_table.lock[hash_key]);

	if (!merge_flag) {
		// Create a new neighbour entry
		entry = neighbour_entry_create(
			device, hash_key, src_paddr, arp_header->plen);
		if (!entry) return PROTO_ERROR_OTHER;

		spin_lock(&entry->lock);
	} else {
	}
	entry->state = NEIGH_STATE_REACHABLE;
	memcpy(entry->haddr, src_haddr, arp_header->hlen);
	spin_unlock(&entry->lock);

	uint16_t opcode = BE2HOST_WORD(arp_header->opcode);
	if (opcode == ARP_OP_REQUEST) {
		memcpy(dst_haddr, src_haddr, arp_header->hlen);
		memcpy(dst_paddr, src_paddr, arp_header->plen);
		memcpy(src_haddr, haddr, arp_header->hlen);
		memcpy(src_paddr, paddr, arp_header->plen);
		arp_header->opcode = HOST2BE_WORD(ARP_OP_REPLY);
		device->ops->send(
			device, net_buffer->head, net_buffer->tail - net_buffer->head);
	}

	return PROTO_OK;
}

void arp_send_request(NeighbourEntry *entry, void *arg) {
	NetworkDevice	  *device	  = entry->device;
	EthernetDevice	  *eth_device = device->ethernet;
	NetworkConnection *conn		  = eth_device->arp_conn;

	// Fill in the ARP request details
	ArpHeader *arp_header = (ArpHeader *)conn_buffer(conn)->data;
	arp_header->htype	  = HOST2BE_WORD(ARP_HTYPE_ETH);
	arp_header->ptype	  = HOST2BE_WORD(ETH_TYPE_IPV4);
	arp_header->hlen	  = 6;
	arp_header->plen	  = 4;
	arp_header->opcode	  = HOST2BE_WORD(ARP_OP_REQUEST);

	void *p			= (void *)(arp_header + 1);
	void *src_haddr = p;
	void *src_paddr = p + arp_header->hlen;
	void *dst_haddr = p + arp_header->hlen + arp_header->plen;
	void *dst_paddr = p + arp_header->hlen * 2 + arp_header->plen;

	// Copy the sender's hardware and protocol addresses
	memcpy(src_haddr, eth_device->mac_addr, arp_header->hlen);
	memcpy(src_paddr, device->ipv4_addr, arp_header->plen);

	// Copy the target's hardware and protocol addresses
	uint8_t eth_dst_addr[ETH_IDENTIFIER_SIZE] = {0};
	memcpy(dst_haddr, eth_dst_addr, arp_header->hlen);
	memcpy(dst_paddr, entry->ip_addr, arp_header->plen);

	uint8_t size =
		sizeof(ArpHeader) + arp_header->hlen * 2 + arp_header->plen * 2;
	net_buffer_put(conn_buffer(conn), size);

	// Send the ARP request
	eth_wrap(
		conn_buffer(conn), device->ethernet->mac_addr, eth_broadcast_mac,
		ETH_TYPE_ARP);
	entry->state = NEIGH_STATE_WAITING;

	NETWORK_SEND(device, eth_device->arp_conn);
}
