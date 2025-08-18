/**
 * @file arp.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief ARP
 *
 * Reference:
 * RFC 826: An Ethernet Address Resolution Protocol
 *
 */
#include <bits.h>
#include <driver/network/conn.h>
#include <driver/network/protocols/arp.h>
#include <driver/network/protocols/protocols.h>
#include <network/eth.h>
#include <stdint.h>
#include <string.h>

ProtocolResult arp_wrap(
	NetworkConnection *conn, uint8_t *dst_haddr, uint8_t *dst_paddr,
	uint16_t opcode) {
	uint16_t htype = 0, ptype = 0;
	uint8_t	 hlen = 0, plen = 0;
	uint8_t *src_haddr, *src_paddr;
	switch (conn->phy_protocol) {
	case PHY_PROTO_ETHERNET:
		src_haddr = conn->ethernet.mac;
		hlen	  = 6;
		htype	  = HOST2BE_WORD(ARP_HTYPE_ETH);
		break;
	default:
		return PROTO_ERROR_UNSUPPORT;
	}
	switch (conn->net_protocol) {
	case NET_PROTO_IPV4:
		src_paddr = conn->ipv4.ip;
		plen	  = 4;
		ptype	  = HOST2BE_WORD(ETH_TYPE_IPV4);
		break;
	default:
		return PROTO_ERROR_UNSUPPORT;
	}

	ArpHeader *arp_header = (ArpHeader *)conn->buffer->data;
	arp_header->htype	  = htype;
	arp_header->ptype	  = ptype;
	arp_header->hlen	  = hlen;
	arp_header->plen	  = plen;
	arp_header->opcode	  = HOST2BE_WORD(opcode);

	void *p = conn->buffer->head + sizeof(ArpHeader);

	memcpy(p, src_haddr, hlen);
	memcpy(p + hlen, src_paddr, plen);
	p += hlen + plen;
	memcpy(p, dst_haddr, hlen);
	memcpy(p + hlen, dst_paddr, plen);

	return PROTO_OK;
}