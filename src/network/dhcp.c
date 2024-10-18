#include <bits.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <network/dhcp.h>
#include <network/eth.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/udp.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t dhcp_magic_cookie[4] = {99, 130, 83, 99};

int dhcp_main(net_device_t *net_dev) {
	uint8_t		*buf	= kmalloc(sizeof(dhcp_basic_t) + 20);
	device_t	*device = net_dev->device;
	netc_t		*netc;
	dhcp_basic_t dhcp;
	dhcp_info_t *dhcp_info = kmalloc(sizeof(dhcp_info_t));
	uint8_t		 option	   = DHCP_OPTION_END, len, msg_type;

	int flag;

	net_dev->info->dhcp_data = dhcp_info;

	memset(&dhcp, 0, sizeof(dhcp_basic_t));
	dhcp.Op = DHCP_OP_REQUEST;
	if (device->type == DEV_ETH_NET) {
		dhcp.htype = 1;
		dhcp.hlen  = 6;
	} else {
		return -1;
	}
	dhcp.hops  = 0;
	dhcp.xID   = HOST2BE_DWORD(rand());
	dhcp.secs  = 0;
	dhcp.flags = HOST2BE_WORD(0x8000);
	memcpy(dhcp.chAddr, net_dev->info->mac, 6);

	memcpy(buf, &dhcp, sizeof(dhcp_basic_t));
	memcpy(buf + sizeof(dhcp_basic_t), dhcp_magic_cookie, 4);
	buf[sizeof(dhcp_basic_t) + 4] = DHCP_OPTION_MSG_TYPE;
	buf[sizeof(dhcp_basic_t) + 5] = 1;
	buf[sizeof(dhcp_basic_t) + 6] = DHCP_DISCOVER;
	buf[sizeof(dhcp_basic_t) + 7] = DHCP_OPTION_END;

	netc = netc_create(net_dev, ETH_TYPE_IPV4, PROTOCOL_UDP);
	netc_set_dest(netc, broadcast_mac, NULL, 0);
	if (udp_bind(netc, broadcast_ipv4_addr, 68, 67) < 0) return -1;

	udp_send(netc, (uint16_t *)buf, sizeof(dhcp_basic_t) + 8);
	netc_read(netc, buf, sizeof(dhcp_basic_t) + 4);
	if (memcmp(buf + sizeof(dhcp_basic_t), dhcp_magic_cookie, 4) != 0) return -2;
	netc_read(netc, &option, sizeof(option));
	netc_read(netc, &len, sizeof(len));
	if (option != DHCP_OPTION_MSG_TYPE) return -3;
	netc_read(netc, &msg_type, len);
	if (msg_type != DHCP_OFFER) return -3;

	memcpy(dhcp_info->server_ip, ((dhcp_basic_t *)buf)->siAddr, 4);

	struct ipv4_data *ipv4 = (struct ipv4_data *)netc->net_dev->info->ipv4_data;
	flag				   = 1;
	while (flag) {
		netc_read(netc, &option, sizeof(option));
		netc_read(netc, &len, sizeof(len));
		switch (option) {
		case DHCP_OPTION_END:
			flag = 0;
			break;
		case DHCP_OPTION_SERVER_ID:
			netc_read(netc, (uint8_t *)&dhcp_info->server_id, 4);
			break;
		case DHCP_OPTION_ROUTER:
			netc_read(netc, ipv4->router_ip, len);
			break;
		case DHCP_OPTION_SUBNET_MASK:
			netc_read(netc, ipv4->subnet_mask, len);
			break;
		case DHCP_OPTION_DOMAIN_SERVER:
			netc_read(netc, ipv4->dns_server_ip, len);
			break;
		case DHCP_OPTION_LEASE_TIME:
			netc_read(netc, (uint8_t *)&dhcp_info->lease_time, 4);
			dhcp_info->lease_time = BE2HOST_DWORD(dhcp_info->lease_time);
			break;

		default:
			break;
		}
	}
	netc_drop_all(netc);

	uint8_t *options = buf + sizeof(dhcp_basic_t) + 4;
	memcpy(buf, &dhcp, sizeof(dhcp_basic_t));
	options[0] = DHCP_OPTION_MSG_TYPE;
	options[1] = 1;
	options[2] = DHCP_REQUEST;
	options[3] = DHCP_OPTION_SERVER_ID;
	options[4] = 4;
	memcpy(options + 5, &dhcp_info->server_id, 4);
	options[9]					  = DHCP_OPTION_LEASE_TIME;
	options[10]					  = 4;
	*((uint32_t *)(options + 11)) = HOST2BE_DWORD(dhcp_info->lease_time);
	options[15]					  = DHCP_OPTION_END;
	udp_send(netc, (uint16_t *)buf, sizeof(dhcp_basic_t) + 20);

	netc_read(netc, buf, sizeof(dhcp_basic_t) + 4);
	if (memcmp(buf + sizeof(dhcp_basic_t), dhcp_magic_cookie, 4) != 0) return -2;
	ipv4_set_ip(netc, ((dhcp_basic_t *)buf)->yiAddr);
	netc_read(netc, &option, sizeof(option));
	netc_read(netc, &len, sizeof(len));
	if (option != DHCP_OPTION_MSG_TYPE) return -3;
	netc_read(netc, &msg_type, len);
	if (msg_type == DHCP_NAK) return -4;
	else if (msg_type != DHCP_ACK) return -3;
	while (1) {
		netc_read(netc, &option, 1);
		if (option != DHCP_OPTION_END) {
			netc_read(netc, &len, 1);
			netc_read(netc, buf, len);
		} else {
			break;
		}
	}

	netc_drop_all(netc);
	udp_unbind(netc);
	netc_delete(netc);

	return 0;
}