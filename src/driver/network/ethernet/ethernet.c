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
#include "driver/network/protocols/ipv4.h"
#include "driver/network/protocols/protocols.h"
#include <bits.h>
#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <driver/network/ethernet/ethernet.h>
#include <driver/network/network_dm.h>
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
	NET_BUF_RESV_HEAD(conn, 14);
}

ProtocolResult eth_wrap(
	NetworkConnection *conn, const uint8_t *dst_addr, uint16_t protocol) {
	conn_header_alloc(conn, 14);
	EthernetHeader *header = (EthernetHeader *)conn->buffer->head;
	memcpy(header->dst_mac, dst_addr, 6);
	memcpy(header->src_mac, conn->ethernet.mac, 6);

	int content_size = CONN_CONTENT_SIZE(conn);
	if (content_size > ETH_MAX_FRAME_SIZE) return PROTO_ERROR_EXCEED_MAX_SIZE;
	if (content_size < ETH_MIN_FRAME_SIZE) {
		// 填充最小帧长度
		memset(conn->buffer->tail, 0, ETH_MIN_FRAME_SIZE - content_size);
		conn->buffer->tail = conn->buffer->head + ETH_MIN_FRAME_SIZE;
	}
	header->protocol_type = HOST2BE_WORD(protocol);

	return PROTO_OK;
}

ProtocolResult eth_recv(NetBuffer *net_buffer) {
	EthernetHeader *header = (EthernetHeader *)net_buffer->data;
	int				size   = net_buffer->tail - net_buffer->data;
	if (size < ETH_HEADER_SIZE) { return PROTO_ERROR_UNSUPPORT; }
	if (size > ETH_MAX_FRAME_SIZE) { return PROTO_ERROR_EXCEED_MAX_SIZE; }

	net_buffer->data += sizeof(EthernetHeader);

	ProtocolResult result = PROTO_OK;
	switch (BE2HOST_WORD(header->protocol_type)) {
	case ETH_PROTO_TYPE_IPV4:
		result = ipv4_recv(net_buffer);
		break;
	case ETH_PROTO_TYPE_ARP:
		break;
	default:
		result = PROTO_ERROR_UNSUPPORT;
	}

	return result;
}
