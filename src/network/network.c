#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <math.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/netpack.h>
#include <network/network.h>
#include <network/tcp.h>
#include <stdint.h>

LIST_HEAD(net_rx_raw_pack_lh);
LIST_HEAD(net_rx_tcp_lh);
LIST_HEAD(net_rx_udp_lh);

const int default_ttl = 64;

uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

net_device_t *default_net_dev;

void init_network(void) {
	default_net_dev = NULL;
	list_init(&eth_dm.dev_listhead);
}

netc_t *netc_create(
	net_device_t *net_dev, uint16_t protocol, uint16_t app_protocol) {
	netc_t *netc = kmalloc(sizeof(netc_t));

	spinlock_init(&netc->spin_lock);

	netc->net_dev		= net_dev;
	netc->thread		= get_current_thread();
	netc->proto_private = NULL;
	netc->app_private	= NULL;
	netc->recv_buffer	= kmalloc(NET_MAX_BUFFER_SIZE);
	netc->recv_offset	= 0;
	netc->recv_len		= 0;
	netc->protocol		= protocol;
	netc->app_protocl	= app_protocol;
	return netc;
}

int netc_delete(netc_t *netc) {
	if (netc->proto_private != NULL) { kfree(netc->proto_private); }
	if (netc->app_private != NULL) { kfree(netc->app_private); }
	kfree(netc);
	return 0;
}

void netc_set_dest(
	netc_t *netc, uint8_t dst_mac[6], uint8_t *dst_laddr,
	uint8_t dst_laddr_len) {
	memcpy(netc->dst_mac, dst_mac, 6);
	if (dst_laddr != NULL) {
		memcpy(netc->dst_laddr, dst_laddr, dst_laddr_len);
	}
}

int netc_read(netc_t *netc, uint8_t *buf, uint32_t size) {
	while (netc->recv_len == 0) {}

	int real_size = MIN(netc->recv_len, size);
	if (netc->recv_offset + real_size > NET_MAX_BUFFER_SIZE) {
		int tmp = NET_MAX_BUFFER_SIZE - netc->recv_offset;
		memcpy(buf, netc->recv_buffer + netc->recv_offset, tmp);
		memcpy(buf + tmp, netc->recv_buffer, real_size - tmp);
		netc->recv_offset = real_size - tmp;
		netc->recv_len -= real_size;
	} else {
		memcpy(buf, netc->recv_buffer + netc->recv_offset, real_size);
		netc->recv_offset += real_size;
		netc->recv_len -= real_size;
	}
	return real_size;
}

void netc_drop_all(netc_t *netc) {
	netc->recv_offset = 0;
	netc->recv_len	  = 0;
}

void netc_ip_send(
	netc_t *netc, uint8_t *ip, uint8_t DF, uint8_t proto, uint8_t *buf,
	uint32_t size) {
	if (netc->protocol == ETH_TYPE_IPV4) {
		ipv4_send(netc, ip, DF, default_ttl, proto, buf, size);
	}
}

int netc_get_mtu(netc_t *netc) {
	switch (netc->protocol) {
	case ETH_TYPE_IPV4:
		return ipv4_get_mtu(netc);
		break;
	default:
		break;
	}
}

void net_process_pack(void *arg) {
	net_rx_pack_t *pack_cur, *next;
	while (1) {
		if (!list_empty(&net_rx_raw_pack_lh)) {
			list_for_each_owner_safe (
				pack_cur, next, &net_rx_raw_pack_lh, list) {
				list_del(&pack_cur->list);
				switch (pack_cur->type) {
				case ETH_FRAME:
					eth_handler(pack_cur, pack_cur->data, pack_cur->data_len);
					break;
				default:
					break;
				}
			}
			list_for_each_owner_safe (pack_cur, next, &net_rx_tcp_lh, list) {
				list_del(&pack_cur->list);
				tcp_recv(
					pack_cur->data, pack_cur->proto_start, pack_cur->data_len,
					pack_cur->src_ip_addr, pack_cur->src_ip_len);
			}
			// kfree(pack_cur->data);
			kfree(pack_cur);
		} else {
			schedule();
		}
	}
}

void net_rx_raw_pack(enum frame_type type, uint8_t *buf, uint32_t length) {

	net_rx_pack_t *pack = kmalloc(sizeof(net_rx_pack_t));
	pack->type			= type;
	pack->data			= buf;
	pack->len			= length;
	list_add_tail(&pack->list, &net_rx_raw_pack_lh);
}

void net_raw2tcp_pack(
	net_rx_pack_t *pack, uint8_t ip_len, uint8_t *ip_addr, uint32_t start,
	uint32_t len) {
	pack->proto_start = start;
	pack->src_ip_len  = ip_len;
	pack->src_ip_addr = ip_addr;
	pack->data_len	  = len;
	list_add_tail(&pack->list, &net_rx_tcp_lh);
}

void net_raw2udp_pack(net_rx_pack_t *pack, uint32_t start, uint32_t len) {
	pack->proto_start = start;
	pack->data_len	  = len;
	list_add_tail(&pack->list, &net_rx_tcp_lh);
}
