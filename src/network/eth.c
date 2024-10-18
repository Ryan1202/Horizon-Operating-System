#include <bits.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/thread.h>
#include <math.h>
#include <network/arp.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/network.h>

void eth_device_register(struct _device_manager_s *dm, device_t *device, char *name);
void eth_device_unregister(struct _device_manager_s *dm, device_t *device);
void eth_driver_inited(struct _device_manager_s *dm);

device_manager_t eth_dm = {
	.name		   = {0, 0, NULL},
	.private_data  = eth_handler,
	.dm_register   = eth_device_register,
	.dm_unregister = eth_device_unregister,
	.drv_inited	   = eth_driver_inited,
};

int eth_read(netc_t *netc, uint8_t *buffer, uint32_t length) {
	// device_t *device = list_owner(&inode->device->listm, device_t, listm);
	// return device->drv_obj->function.driver_read(device, buffer, 0, length);
	return 0;
}

int eth_write(netc_t *netc, uint8_t *buffer, uint32_t length) {
	eth_frame_t *frame;
	uint16_t	 size = MAX(ETH_MIN_FRAME_SIZE, length + 14);
	uint8_t		*buf  = kmalloc(size);
	frame			  = (eth_frame_t *)buf;

	memcpy(frame->src_mac, netc->net_dev->info->mac, 6);
	memcpy(frame->dest_mac, netc->dst_mac, 6);
	frame->type = HOST2BE_WORD(netc->protocol);
	memcpy(buf + 14, buffer, length);
	memset(buf + length + 14, 0, size - length - 14);
	DEV_WRITE(netc->net_dev->device, buf, 0, size);
	kfree(buf);
	return 0;
}

void eth_handler(net_rx_pack_t *pack, uint8_t *buf, uint16_t size) {
	eth_frame_t *frame;

	frame		= (eth_frame_t *)buf;
	frame->type = BE2HOST_WORD(frame->type);
	switch (frame->type) {
	case ETH_TYPE_IPV4:
		ipv4_read(pack, buf, sizeof(eth_frame_t), size - sizeof(eth_frame_t));
		break;
	case ETH_TYPE_ARP:
		arp_read(buf, sizeof(eth_frame_t), size - sizeof(eth_frame_t));
		break;

	default:
		break;
	}
}

void eth_device_register(struct _device_manager_s *dm, device_t *device, char *name) {
	net_device_t *net_dev;
	string_init(&device->name);
	string_new(&device->name, name, strlen(name));
	net_dev			= kmalloc(sizeof(net_device_t));
	net_dev->info	= kmalloc(sizeof(struct network_info));
	net_dev->device = device;
	net_dev->enable = 1;
	list_add_tail(&net_dev->list, &dm->dev_listhead);
	ipv4_init(net_dev);

	if (default_net_dev == NULL) { default_net_dev = net_dev; }
}

void eth_device_unregister(struct _device_manager_s *dm, device_t *device) {
	net_device_t *cur, *next;
	string_del(&device->name);
	list_for_each_owner_safe (cur, next, &dm->dev_listhead, list) {
		if (cur->device == device) {
			list_del(&cur->list);
			return;
		}
	}
}

void eth_driver_inited(struct _device_manager_s *dm) {
	status_t	  stat;
	net_device_t *cur, *next;
	list_for_each_owner_safe (cur, next, &dm->dev_listhead, list) {
		stat = cur->device->drv_obj->function.driver_open(cur->device);
		DEV_CTL(cur->device, NET_FUNC_GET_MAC_ADDR, cur->info->mac);
		if (stat != SUCCUESS) {
			cur->enable = 0;
		} else {
			cur->net_read  = &eth_read;
			cur->net_write = &eth_write;
		}
	}
}
