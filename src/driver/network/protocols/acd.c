/**
 * @file acd.c
 * @author your name (you@domain.com)
 * @brief Address Conflict Detection
 * References:
 * - RFC5227: IPv4 Address Conflict Detection
 */
#include "driver/network/ethernet/ethernet.h"
#include "driver/network/neighbour.h"
#include "driver/network/network_dm.h"
#include "driver/network/protocols/ipv4.h"
#include "driver/network/protocols/protocols.h"
#include "driver/timer_dm.h"
#include <driver/network/protocols/acd.h>
#include <driver/network/protocols/arp.h>
#include <stdint.h>

void acd_timer_callback(void *arg) {
	NetworkDevice  *device	   = arg;
	EthernetDevice *eth_device = device->ethernet;

	switch (eth_device->acd_state) {
	case ACD_STATE_PROBE:
		if (eth_device->probe_count < ACD_PROBE_NUM) {
			eth_device->probe_count++;
			timer_set_timeout(&eth_device->timer, ACD_PROBE_MIN * 1000);
			timer_callback_enable(&eth_device->timer);

			NeighbourKey	key = ipv4_hash(device->ipv4.ip);
			NeighbourEntry *entry =
				neighbour_table_lookup(device, key, device->ipv4.ip, 4);
			arp_send_request(entry, arg);
		} else {
			// 确认没有冲突，发送公告
			acd_announce(device);
		}
		break;
	case ACD_STATE_ANNOUNCE:
		if (eth_device->announce_count < ACD_ANNOUNCE_NUM) {
			eth_device->announce_count++;
			timer_set_timeout(&eth_device->timer, ACD_ANNOUNCE_INTERVAL * 1000);
			timer_callback_enable(&eth_device->timer);
			arp_announce(device, device->ipv4.ip);
		} else {
			eth_device->acd_state = ACD_STATE_NONE;
		}
		break;
	default:
		break;
	}
}

ProtocolResult acd_conflict_detected(NetworkDevice *device) {
	EthernetDevice *eth_device = device->ethernet;
	timer_callback_cancel(&eth_device->timer);
	eth_device->acd_state = ACD_STATE_CONFLICT;
	if (eth_device->acd_conflict_callback)
		eth_device->acd_conflict_callback(device);
	return PROTO_OK;
}

void acd_probe(NetworkDevice *device) {
	EthernetDevice *eth_device = device->ethernet;
	eth_device->probe_count	   = 0;

	eth_device->timer.callback = acd_timer_callback;
	eth_device->timer.arg	   = device;

	eth_device->acd_state = ACD_STATE_PROBE;
	timer_set_timeout(&eth_device->timer, ACD_PROBE_WAIT * 1000);
	timer_callback_enable(&eth_device->timer);
}

void acd_announce(NetworkDevice *device) {
	EthernetDevice *eth_device = device->ethernet;
	eth_device->announce_count = 0;

	eth_device->timer.callback = acd_timer_callback;
	eth_device->timer.arg	   = device;

	eth_device->acd_state = ACD_STATE_ANNOUNCE;
	timer_set_timeout(&eth_device->timer, ACD_ANNOUNCE_WAIT * 1000);
	timer_callback_enable(&eth_device->timer);
}
