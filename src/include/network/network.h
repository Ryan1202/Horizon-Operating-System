#ifndef NETWORK_H
#define NETWORK_H

#include <kernel/driver.h>
#include <kernel/list.h>
#include <stdint.h>

#define ETH_MAX_FRAME_SIZE 1536
#define ETH_MIN_FRAME_SIZE 60

#define NET_FUNC_GET_MAC_ADDR 0x01
#define NET_FUNC_SET_MAC_ADDR 0x02

#define ETH_TYPE_IPV4	0x0800
#define ETH_TYPE_IPV6	0x86dd
#define ETH_TYPE_ARP	0x0806
#define ETH_TYPE_802_1Q 0x8100

#define SWAP_WORD(n)  ((((n)&0xff) << 8) | (((n)&0xff00) >> 8))
#define SWAP_DWORD(n) (SWAP_WORD((n)&0xffff) << 16 | SWAP_WORD((n)&0xffff0000) >> 16)

#ifdef ARCH_X86
#define BE2HOST_WORD(n)	 SWAP_WORD(n)
#define BE2HOST_DWORD(n) SWAP_DWORD(n)
#define LE2HOST_WORD(n)	 (n)
#define LE2HOST_DWORD(n) (n)

#define HOST2BE_WORD(n)	 SWAP_WORD(n)
#define HOST2BE_DWORD(n) SWAP_DWORD(n)
#define HOST2LE_WORD(n)	 (n)
#define HOST2LE_DWORD(n) (n)
#endif

typedef struct {
	uint8_t	 dest_mac[6];
	uint8_t	 src_mac[6];
	uint16_t type;
	list_t	 list;
} __attribute__((packed)) eth_frame_t;

extern driver_manager_t eth_dm;
extern device_t		   *default_net_device;

void init_network(void);
void eth_write(device_t *device, uint8_t *src_mac, uint8_t *dst_mac, uint16_t type, uint8_t *data,
			   uint8_t length);

#endif