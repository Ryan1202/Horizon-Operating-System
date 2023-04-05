#ifndef IPV4_H
#define IPV4_H

#include "../stdint.h"
#include "network.h"

#define IPV4_FLAG_DF 0x02
#define IPV4_FLAG_MF 0x04

typedef struct {
	uint8_t	 IHL	 : 4;
	uint8_t	 Version : 4;
	uint8_t	 TypeOfService;
	uint16_t TotalLength;
	uint16_t Identification;
	uint16_t Offset;
	uint8_t	 TimetoLive;
	uint8_t	 Protocol;
	uint16_t HeaderChecksum;
	uint8_t	 SourceAddress[4];
	uint8_t	 DestinationAddress[4];
	uint8_t *Options;
} __attribute__((packed)) ipv4_header_t;

struct ipv4_data {
	uint8_t	   ip_addr[4];
	uint8_t	   subnet_mask[4];
	uint8_t	   router_ip[4];
	uint8_t	   dns_server_ip[4];
	spinlock_t idlock;
	uint16_t   counter;
};

typedef struct {
	uint16_t src_port, dst_port;
	list_t	 list;
} ipv4_conn_t;

extern uint8_t broadcast_ipv4_addr[4];

void ipv4_init(struct network_info *net);
int	 ipv4_send(netc_t *netc, uint8_t *dst_ip, uint8_t DF, uint8_t ttl, uint8_t protocol, uint8_t *data,
			   uint32_t datalen);
void ipv4_read(uint8_t *buf, uint16_t offset, uint16_t length);
void ipv4_get_ip(netc_t *netc, uint8_t *ip);
void ipv4_set_ip(netc_t *netc, uint8_t *ip);

#endif