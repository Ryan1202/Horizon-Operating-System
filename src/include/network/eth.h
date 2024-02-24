#ifndef ETH_H
#define ETH_H

#include <kernel/driver.h>
#include <kernel/list.h>
#include <network/netpack.h>
#include <network/network.h>
#include <stdint.h>

#define ETH_MAX_FRAME_SIZE 1536
#define ETH_MIN_FRAME_SIZE 60
#define ETH_MTU			   1500

#define ETH_TYPE_IPV4	0x0800
#define ETH_TYPE_IPV6	0x86dd
#define ETH_TYPE_ARP	0x0806
#define ETH_TYPE_802_1Q 0x8100

typedef void (*eth_handler_t)(device_manager_t *eth_dm, uint8_t *buf, uint16_t size);

typedef struct eth_frame_s {
	uint8_t	 dest_mac[6];
	uint8_t	 src_mac[6];
	uint16_t type;
} __attribute__((packed)) eth_frame_t;

extern device_manager_t eth_dm;

void eth_handler(net_rx_pack_t *pack, uint8_t *buf, uint16_t size);
int	 eth_read(netc_t *netc, uint8_t *buffer, uint32_t length);
int	 eth_write(netc_t *netc, uint8_t *buffer, uint32_t length);

#endif