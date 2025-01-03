#ifndef NETWORK_H
#define NETWORK_H

#include "kernel/spinlock.h"
#include <config.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/thread.h>
#include <kernel/wait_queue.h>
#include <stdint.h>

#define NET_MAX_BUFFER_SIZE 4096

#define NET_FUNC_GET_MTU	  0x00
#define NET_FUNC_GET_MAC_ADDR 0x01
#define NET_FUNC_SET_MAC_ADDR 0x02

struct network_info {
	uint8_t mac[6];
	void   *ipv4_data;
	void   *dhcp_data;

	list_t list;
};

typedef struct netc_s {
	struct task_s		*thread;
	struct net_device_s *net_dev;

	spinlock_t spin_lock;

	uint8_t *recv_buffer;
	uint32_t recv_offset;
	uint32_t recv_len;

	uint16_t protocol;
	uint16_t proto_id;
	list_t	 proto_list;
	void	*proto_private;
	uint16_t app_protocl;
	uint32_t app_proto_id;
	void	*app_private;

	uint8_t	 dst_mac[6];
	uint8_t *dst_laddr, dst_laddr_len;
} netc_t;

typedef struct net_device_s {
	struct network_info *info;
	device_t			*device;
	int					 enable;

	list_t list;

	int (*net_read)(struct netc_s *netc, uint8_t *buffer, uint32_t length);
	int (*net_write)(struct netc_s *netc, uint8_t *buffer, uint32_t length);
} net_device_t;

extern net_device_t *default_net_dev;

extern uint8_t broadcast_mac[6];

void	init_network(void);
netc_t *netc_create(net_device_t *net_dev, uint16_t protocol, uint16_t app_protocol);
int		netc_delete(netc_t *netc);
void	netc_set_dest(netc_t *netc, uint8_t dst_mac[6], uint8_t *dst_laddr, uint8_t dst_laddr_len);
int		netc_read(netc_t *netc, uint8_t *buf, uint32_t size);
void	netc_drop_all(netc_t *netc);
void	netc_ip_send(netc_t *netc, uint8_t *ip, uint8_t DF, uint8_t proto, uint8_t *buf, uint32_t size);
int		netc_get_mtu(netc_t *netc);

#endif