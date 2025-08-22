#ifndef _NETWORK_ETHERNET_H
#define _NETWORK_ETHERNET_H

#include "driver/network/buffer.h"
#include "driver/network/network_dm.h"
#include "driver/network/protocols/protocols.h"
#include "driver/timer_dm.h"
#include <stdint.h>

#define ETH_MAX_FRAME_SIZE	1792
#define ETH_MIN_FRAME_SIZE	60
#define ETH_IDENTIFIER_SIZE 6

#define ETH_HEADER_SIZE 14

#define ETH_PROTO_TYPE_IPV4 0x0800
#define ETH_PROTO_TYPE_ARP	0x0806

typedef struct EthernetHeader {
	uint8_t	 dst_mac[ETH_IDENTIFIER_SIZE];
	uint8_t	 src_mac[ETH_IDENTIFIER_SIZE];
	uint16_t protocol_type; // Network layer protocol type
} EthernetHeader;

typedef struct EthernetDevice {
	Timer	timer;
	uint8_t probe_count, announce_count;
	enum {
		ACD_STATE_NONE,
		ACD_STATE_PROBE,
		ACD_STATE_ANNOUNCE,
		ACD_STATE_IN_USE,
	} acd_state;

	struct NetworkConnection *arp_conn;
	NetworkDevice			 *net_device;
	uint8_t					  mac_addr[ETH_IDENTIFIER_SIZE];
} EthernetDevice;

void eth_set_mac_address(EthernetDevice *device, uint8_t *mac_addr);
void eth_get_mac_address(EthernetDevice *device, uint8_t *mac_addr);

struct NetworkConnection;

extern const uint8_t eth_broadcast_mac[6];

void		   eth_register(struct NetworkConnection *conn);
ProtocolResult eth_wrap(
	struct NetBuffer *buffer, uint8_t *mac_addr, const uint8_t *dst_addr,
	uint16_t protocol);
ProtocolResult eth_recv(NetworkDevice *device, NetBuffer *net_buffer);

#endif