#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/thread.h>
#include <math.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/network.h>

uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

net_device_t *default_net_dev;

void init_network(void) {
	net_device_t *net_dev;
	default_net_dev = NULL;
	list_init(&eth_dm.dev_listhead);
}

netc_t *netc_create(net_device_t *net_dev, uint16_t protocol, uint16_t app_protocol) {
	netc_t *netc = kmalloc(sizeof(netc_t));

	wait_queue_init(&net_dev->wqm);

	netc->net_dev	  = net_dev;
	netc->thread	  = get_current_thread();
	netc->recv_buffer = kmalloc(NET_MAX_BUFFER_SIZE);
	netc->recv_offset = 0;
	netc->recv_len	  = 0;
	netc->protocol	  = protocol;
	netc->app_protocl = app_protocol;
	return netc;
}

void netc_set_dest(netc_t *netc, uint8_t dst_mac[6]) {
	memcpy(netc->dst_mac, dst_mac, 6);
}

int netc_read(netc_t *netc, uint8_t *buf, uint32_t size) {
	if (netc->recv_len == 0) {
		wait_queue_add(&netc->net_dev->wqm, 0);
		thread_block(TASK_BLOCKED);
	}

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
