#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/tcp.h>
#include <network/udp.h>

uint8_t broadcast_ipv4_addr[4] = {0xff, 0xff, 0xff, 0xff};

void ipv4_init(struct network_info *net) {
	struct ipv4_data *data;

	net->ipv4_data = kmalloc(sizeof(struct ipv4_data));
	data		   = (struct ipv4_data *)net->ipv4_data;
	memset(data->ip_addr, 0, 4);
	spinlock_init(&data->idlock);
	data->counter = 0;
	memset(data->ip_addr, 0, 6);
}

void ipv4_send_pack(netc_t *netc, uint8_t *dst_ip, uint8_t flags, uint8_t id, uint8_t ttl, uint8_t protocol,
					uint8_t offset, uint8_t *data, uint16_t datalen) {
	int				  i;
	uint32_t		  chksum = 0;
	ipv4_header_t	 *header;
	net_device_t	 *net_dev = netc->net_dev;
	struct ipv4_data *ipv4	  = (struct ipv4_data *)net_dev->info->ipv4_data;

	uint16_t *buf = kmalloc(20 + datalen);
	header		  = (ipv4_header_t *)buf;

	header->Version		   = 4;
	header->IHL			   = 20 / 4;
	header->TypeOfService  = 0;
	header->TotalLength	   = HOST2BE_WORD(20 + datalen);
	header->Identification = HOST2BE_WORD(id);
	header->Offset		   = HOST2BE_WORD((offset >> 3) | (flags << 13));
	header->TimetoLive	   = ttl;
	header->Protocol	   = protocol;
	header->HeaderChecksum = 0; // 先置0防止影响checksum计算
	memcpy(header->SourceAddress, ipv4->ip_addr, 4);
	memcpy(header->DestinationAddress, dst_ip, 4);
	memcpy((uint8_t *)buf + 20, data + offset, datalen);

	for (i = 0; i < 20 / sizeof(uint16_t); i++) {
		chksum += BE2HOST_WORD(buf[i]);
	}
	header->HeaderChecksum = HOST2BE_WORD(~(uint16_t)((chksum & 0xffff) + (chksum >> 16)));

	net_dev->net_write(netc, (uint8_t *)buf, datalen + 20);

	kfree(buf);
}

int ipv4_send(netc_t *netc, uint8_t *dst_ip, uint8_t DF, uint8_t ttl, uint8_t protocol, uint8_t *data,
			  uint32_t datalen) {
	uint32_t		  i, id;
	uint32_t		  mtu, len;
	net_device_t	 *net_dev = netc->net_dev;
	struct ipv4_data *ipv4	  = (struct ipv4_data *)net_dev->info->ipv4_data;

	if (netc->dst_mac[0] == 0) return -1;

	i	= 0;
	len = datalen;
	DEV_CTL(net_dev->device, NET_FUNC_GET_MTU, &mtu);
	mtu -= 20; // 去掉ip头的长度

	spin_lock(&ipv4->idlock);
	id = ipv4->counter++;
	spin_unlock(&ipv4->idlock);

	while (len > mtu) {
		if (DF) { return -1; } // 不允许分片则直接退出
		ipv4_send_pack(netc, dst_ip, 1, id, ttl, protocol, i * mtu, data, mtu);
		i++;
	}
	ipv4_send_pack(netc, dst_ip, 0, id, ttl, protocol, i * mtu, data, datalen % mtu);
	return 0;
}

void ipv4_read(uint8_t *buf, uint16_t offset, uint16_t length) {
	ipv4_header_t *header = (ipv4_header_t *)(buf + offset);

	header->HeaderChecksum = BE2HOST_WORD(header->HeaderChecksum);
	header->Identification = BE2HOST_WORD(header->Identification);
	header->Offset		   = BE2HOST_WORD(header->Offset);
	header->TotalLength	   = BE2HOST_WORD(header->TotalLength);

	switch (header->Protocol) {
	case PROTOCOL_TCP:
		tcp_read(buf, offset + 20, length - 20);
		break;
	case PROTOCOL_UDP:
		udp_read(buf, offset + 20, length - 20);
		break;

	default:
		break;
	}
}

void ipv4_get_ip(netc_t *netc, uint8_t *ip) {
	struct ipv4_data *ipv4 = (struct ipv4_data *)netc->net_dev->info->ipv4_data;
	memcpy(ip, ipv4->ip_addr, 4);
}

void ipv4_set_ip(netc_t *netc, uint8_t *ip) {
	struct ipv4_data *ipv4 = (struct ipv4_data *)netc->net_dev->info->ipv4_data;
	memcpy(ipv4->ip_addr, ip, 4);
}
