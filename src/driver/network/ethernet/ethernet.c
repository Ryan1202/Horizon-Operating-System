/**
 * @file ethernet.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief Ethernet II
 *
 * Reference:
 * IEEE 802.3: IEEE Standard for Ethernet
 *    RFC 894: A Standard for the Transmission of IP Datagrams over Ethernet
 *             Networks
 *
 */
#include "driver/network/protocols/ipv4/ipv4.h"
#include "driver/network/protocols/protocols.h"
#include <bits.h>
#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <driver/network/ethernet/ethernet.h>
#include <driver/network/network_dm.h>
#include <driver/network/protocols/ipv4/arp.h>
#include <kernel/device.h>
#include <objects/handle.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <stdint.h>
#include <string.h>

NetProtocol ethernet_protocol = {
	.head_size = 14,
	.tail_size = 0,
};

const uint8_t eth_broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8_t eth_null_mac[6]	   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void eth_set_mac_address(EthernetDevice *device, uint8_t *mac_addr) {
	memcpy(device->mac_addr, mac_addr, 6);
}

void eth_get_mac_address(EthernetDevice *device, uint8_t *mac_addr) {
	memcpy(mac_addr, device->mac_addr, 6);
}

void eth_register(NetworkConnection *conn) {
	EthernetDevice *eth_device = conn->net_device->ethernet;
	conn->phy_protocol		   = PHY_PROTO_ETHERNET;
	eth_get_mac_address(eth_device, conn->ethernet.mac);
	conn->mtu = ETH_MTU;
	NET_BUF_RESV_HEAD(conn, 14);
}

ProtocolResult eth_wrap(
	NetBuffer *buffer, uint8_t *mac_addr, const uint8_t *dst_addr,
	uint16_t protocol) {
	net_buffer_header_alloc(buffer, 14);
	EthernetHeader *header = (EthernetHeader *)buffer->head;
	memcpy(header->dst_mac, dst_addr, 6);
	memcpy(header->src_mac, mac_addr, 6);

	int content_size = buffer->tail - buffer->head;
	if (content_size > ETH_MAX_FRAME_SIZE) return PROTO_ERROR_EXCEED_MAX_SIZE;
	if (content_size < ETH_MIN_FRAME_SIZE) {
		// 填充最小帧长度
		memset(buffer->tail, 0, ETH_MIN_FRAME_SIZE - content_size);
	}
	header->protocol_type = HOST2BE_WORD(protocol);

	return PROTO_OK;
}

ProtocolResult eth_recv(NetworkDevice *device, NetBuffer *net_buffer) {
	EthernetHeader *header = (EthernetHeader *)net_buffer->data;
	int				size   = net_buffer->tail - net_buffer->data;
	if (size < ETH_HEADER_SIZE) { return PROTO_ERROR_UNSUPPORT; }
	if (size > ETH_MAX_FRAME_SIZE) { return PROTO_ERROR_EXCEED_MAX_SIZE; }

	net_buffer->data += sizeof(EthernetHeader);

	ProtocolResult result = PROTO_OK;
	switch (BE2HOST_WORD(header->protocol_type)) {
	case ETH_PROTO_TYPE_IPV4:
		ipv4_neigh_update(
			device, net_buffer, header->src_mac, ETH_IDENTIFIER_SIZE);
		result = ipv4_recv(device, net_buffer);
		break;
	case ETH_PROTO_TYPE_ARP:
		result = arp_recv(device, net_buffer);
		break;
	default:
		result = PROTO_ERROR_UNSUPPORT;
	}

	return result;
}
