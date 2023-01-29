#include <network/arp.h>
#include <network/network.h>

void send_arp(device_t *device, arp_t *pack) {
	int			i;
	eth_frame_t frame;
	for (i = 0; i < 6; i++) {
		frame.dest_mac[i] = pack->dst_hw_addr[i];
		frame.src_mac[i]  = pack->src_hw_addr[i];
	}
	frame.type = ETH_TYPE_ARP;
	eth_write(device, &frame, (uint8_t *)pack, sizeof(arp_t));
}