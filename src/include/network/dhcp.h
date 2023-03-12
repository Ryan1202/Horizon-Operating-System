#ifndef DHCP_H
#define DHCP_H

#include "../stdint.h"
#include <network/network.h>

#define DHCP_OP_REQUEST 1
#define DHCP_OP_REPLY	2

#define DHCP_DISCOVER	1
#define DHCP_OFFER		2
#define DHCP_REQUEST	3
#define DHCP_DECLINE	4
#define DHCP_ACK		5
#define DHCP_NAK		6
#define DHCP_RELEASE	7
#define DHCP_INFORM		8
#define DHCP_FORCERENEW 9

#define DHCP_OPTION_SUBNET_MASK	  1
#define DHCP_OPTION_TIME_OFFSET	  2
#define DHCP_OPTION_ROUTER		  3
#define DHCP_OPTION_DOMAIN_SERVER 6
#define DHCP_OPTION_LEASE_TIME	  51
#define DHCP_OPTION_MSG_TYPE	  53
#define DHCP_OPTION_SERVER_ID	  54
#define DHCP_OPTION_END			  255

typedef struct {
	uint8_t	 Op;
	uint8_t	 htype, hlen, hops;
	uint32_t xID;
	uint16_t secs;
	uint16_t flags;
	uint8_t	 ciAddr[4];	 // 客户端IP地址
	uint8_t	 yiAddr[4];	 // 服务器分配给客户端的IP地址
	uint8_t	 siAddr[4];	 // 下一个服务器的IP地址
	uint8_t	 giAddr[4];	 // 第一个中继的IP地址
	uint8_t	 chAddr[16]; // 客户端的MAC地址
	uint8_t	 sname[64];
	uint8_t	 file[128];
} __attribute__((packed)) dhcp_basic_t;

typedef struct {
	uint8_t	 server_ip[4];
	uint8_t	 server_id;
	uint32_t lease_time;
} dhcp_info_t;

int dhcp_main(net_device_t *net_dev);

#endif