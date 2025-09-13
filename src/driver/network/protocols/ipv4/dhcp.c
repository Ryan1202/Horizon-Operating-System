/**
 * @file dhcp.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief DHCP
 *
 * Reference:
 * RFC 2131: Dynamic Host Configuration Protocol
 * RFC 1533: DHCP Options and BOOTP Vendor Extensions
 */
#include "bits.h"
#include "driver/network/buffer.h"
#include "driver/network/ethernet/ethernet.h"
#include "driver/network/neighbour.h"
#include "driver/network/protocols/ipv4/acd.h"
#include "driver/timer_dm.h"
#include "kernel/driver_interface.h"
#include "kernel/thread.h"
#include "objects/transfer.h"
#include <driver/network/conn.h>
#include <driver/network/network_dm.h>
#include <driver/network/protocols/ipv4/dhcp.h>
#include <driver/network/protocols/ipv4/ipv4.h>
#include <driver/network/protocols/protocols.h>
#include <driver/network/protocols/udp.h>
#include <kernel/memory.h>
#include <random.h>
#include <stdint.h>
#include <string.h>

#define OPTION_SET_BASE(buf, len, type, value) \
	{                                          \
		*buf = type;                           \
		buf++;                                 \
		*buf = len;                            \
		buf++;                                 \
		*buf = value;                          \
		buf++;                                 \
	}
#define OPTION_SET_HEADER(buf, len, type) \
	{                                     \
		*buf = type;                      \
		buf++;                            \
		*buf = len;                       \
		buf++;                            \
	}

#define DHCP_INIT_TIMEOUT(dhcp)                   \
	({                                            \
		dhcp->retry_times = 0;                    \
		DHCP_RETRANSMIT_DELAY(dhcp->retry_times); \
	})
#define DHCP_SET_TIMEOUT(dhcp, timeout)                   \
	{                                                     \
		timer_set_timeout(&dhcp->timeout_timer, timeout); \
		timer_callback_enable(&dhcp->timeout_timer);      \
	}

static uint8_t dhcp_discover_option_list[] = {
	DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_BROADCAST_ADDRESS,
	DHCP_OPTION_DOMAIN_NAME_SERVER, DHCP_OPTION_NTP_SERVERS};
const int dhcp_discover_option_list_size = sizeof(dhcp_discover_option_list);

ProtocolResult dhcp_discover(DhcpClient *dhcp);
ProtocolResult dhcp_select(DhcpClient *dhcp);
void *dhcp_create_message(DhcpClient *dhcp, DhcpHeader *header, uint8_t op);
ProtocolResult dhcp_send(DhcpClient *dhcp, uint16_t len);
void		   dhcp_reset(DhcpClient *dhcp, NetworkConnection *conn);

ProtocolResult dhcp_retransmit(DhcpClient *dhcp) {
	TransferResult result = NETWORK_SEND(dhcp->device, dhcp->conn);
	if (result != TRANSFER_OK) {
		printk("[DHCP] Failed to send DHCP message: %d\n", result);
		return PROTO_ERROR_SEND_FAILED;
	}

	int timeout = DHCP_RETRANSMIT_DELAY(dhcp->retry_times);
	DHCP_SET_TIMEOUT(dhcp, timeout);

	return PROTO_OK;
}

void dhcp_timeout_handler(void *arg) {
	DhcpClient *dhcp = arg;
	if (dhcp != NULL) {
		dhcp->retry_times++;
		switch (dhcp->state) {
		case DHCP_STAT_INIT:
			// 刚发送完DHCPDECLINE，需要重新请求
			dhcp_reset(dhcp, dhcp->conn);
			dhcp_discover(dhcp);
			break;
		case DHCP_STAT_SELECTING:
		case DHCP_STAT_REQUESTING:
		case DHCP_STAT_RENEWING:
		case DHCP_STAT_REBINDING:
			dhcp_retransmit(dhcp);
			break;
		}
	}
}

void dhcp_ip_conflict_handler(void *arg) {
	DhcpClient *dhcp = arg;

	// 发送DHCPDECLINE
	if (dhcp->state == DHCP_STAT_BOUND) {
		memset(dhcp->ip_addr, ipv4_null_addr, 4);
		memset(dhcp->server_ip_addr, ipv4_null_addr, 4);
		uint8_t *ptr =
			dhcp_create_message(dhcp, dhcp->conn->buffer->data, DHCP_DECLINE);

		*ptr = DHCP_OPTION_END;

		int len = 4 /* magic cookie */
				+ 3 /* message type */
				+ 1 /* end */;
		dhcp_send(dhcp, sizeof(DhcpHeader) + len);

		// 至少等待10秒
		dhcp->state = DHCP_STAT_INIT;
		timer_set_timeout(&dhcp->timeout_timer, 10 * 1000);
		timer_callback_enable(&dhcp->timeout_timer);
	}
}

void dhcp_ip_lease_handler(void *arg) {
	DhcpClient *dhcp = arg;
	if (dhcp->state == DHCP_STAT_REBINDING) {
		dhcp_reset(dhcp, dhcp->conn);
		dhcp_discover(dhcp);
	} else {
		print_error(
			"DHCP", "Lease expired in unexpected state: %d\n", dhcp->state);
	}
}

void dhcp_renew_handler(void *arg) {
	DhcpClient *dhcp = arg;
	if (dhcp->state == DHCP_STAT_BOUND) {
		// TODO: 单播
		uint8_t *ptr =
			dhcp_create_message(dhcp, dhcp->conn->buffer->data, DHCP_REQUEST);

		// TODO: 设置Max Message Size

		OPTION_SET_HEADER(
			ptr, dhcp_discover_option_list_size,
			DHCP_OPTION_PARAMETER_REQUEST_LIST);
		for (int i = 0; i < dhcp_discover_option_list_size; i++, ptr++) {
			*ptr = dhcp_discover_option_list[i];
		}

		*ptr = DHCP_OPTION_END;

		int len = 4 /* magic cookie */
				+ 3 /* message type */
				+ 2 +
				  dhcp_discover_option_list_size /* parameter request list */
				+ 1 /* end */;

		int initial_timeout = DHCP_INIT_TIMEOUT(dhcp);
		DHCP_SET_TIMEOUT(dhcp, initial_timeout);

		dhcp->state			  = DHCP_STAT_RENEWING;
		ProtocolResult result = dhcp_send(dhcp, sizeof(DhcpHeader) + len);
		if (result != PROTO_OK) return;
	}
}

void dhcp_rebind_handler(void *arg) {
	DhcpClient *dhcp = arg;
	if (dhcp->state == DHCP_STAT_RENEWING) {
		// TODO: 广播
		uint8_t *ptr =
			dhcp_create_message(dhcp, dhcp->conn->buffer->data, DHCP_REQUEST);

		// TODO: 设置Max Message Size

		OPTION_SET_HEADER(
			ptr, dhcp_discover_option_list_size,
			DHCP_OPTION_PARAMETER_REQUEST_LIST);
		for (int i = 0; i < dhcp_discover_option_list_size; i++, ptr++) {
			*ptr = dhcp_discover_option_list[i];
		}

		*ptr = DHCP_OPTION_END;

		int len = 4 /* magic cookie */
				+ 3 /* message type */
				+ 2 +
				  dhcp_discover_option_list_size /* parameter request list */
				+ 1 /* end */;

		int initial_timeout = DHCP_INIT_TIMEOUT(dhcp);
		DHCP_SET_TIMEOUT(dhcp, initial_timeout);

		dhcp->state			  = DHCP_STAT_REBINDING;
		ProtocolResult result = dhcp_send(dhcp, sizeof(DhcpHeader) + len);
		if (result != PROTO_OK) return;
	}
}

void dhcp_offer_handler(
	DhcpClient *dhcp, NetworkConnection *conn, DhcpHeader *header,
	uint16_t indexes[DOI_MAX]) {
	// 直接选择收到的第一个OFFER的DHCP服务器

	int index_sid = indexes[DOI_SERVER_ID];
	if (index_sid != 0) {
		timer_callback_cancel(&dhcp->timeout_timer);

		memcpy(dhcp->server_ip_addr, &header->options[index_sid], 4);
		memcpy(dhcp->ip_addr, &header->yiaddr, 4);

		dhcp_select(dhcp);
	}
}

void dhcp_ack_handler(
	DhcpClient *dhcp, NetworkConnection *conn, DhcpHeader *header,
	uint16_t indexes[DOI_MAX]) {
	NetworkDevice *device				  = conn->net_device;
	*(uint32_t *)device->ipv4.subnet_mask = 0;
	*(uint32_t *)device->ipv4.gateway_ip  = 0;
	memcpy(device->ipv4.ip, dhcp->ip_addr, 4);

	if (indexes[DOI_SUBNET_MASK] != 0) {
		*(uint32_t *)device->ipv4.subnet_mask =
			*(uint32_t *)&header->options[indexes[DOI_SUBNET_MASK]];
	}

	if (indexes[DOI_ROUTER] != 0) {
		*(uint32_t *)device->ipv4.gateway_ip =
			*(uint32_t *)&header->options[indexes[DOI_ROUTER]];
	}

	if (indexes[DOI_IP_LEASE_TIME] != 0) {
		dhcp->t0 = BE2HOST_DWORD(
			*(uint32_t *)&header->options[indexes[DOI_IP_LEASE_TIME]]);
	}

	if (indexes[DOI_RENEWAL_TIME] != 0) {
		dhcp->t1 = BE2HOST_DWORD(
			*(uint32_t *)&header->options[indexes[DOI_RENEWAL_TIME]]);
	} else {
		dhcp->t1 = dhcp->t0 / 2;
	}

	if (indexes[DOI_REBINDING_TIME] != 0) {
		dhcp->t2 = BE2HOST_DWORD(
			*(uint32_t *)&header->options[indexes[DOI_REBINDING_TIME]]);
	} else {
		dhcp->t2 = dhcp->t0 * 7 / 8;
	}
}

void dhcp_nak_handler(DhcpClient *dhcp, NetworkConnection *conn) {
	dhcp_reset(dhcp, conn);

	dhcp_discover(dhcp);
}

void dhcp_set_timers(DhcpClient *dhcp) {
	timer_set_timeout(&dhcp->lease_timer, dhcp->t0 * 1000);
	timer_callback_enable(&dhcp->lease_timer);

	timer_set_timeout(&dhcp->renew_timer, dhcp->t1 * 1000);
	timer_callback_enable(&dhcp->renew_timer);

	timer_set_timeout(&dhcp->rebind_timer, dhcp->t2 * 1000);
	timer_callback_enable(&dhcp->rebind_timer);
}

void dhcp_parse_options(
	NetBuffer *net_buffer, DhcpHeader *header, uint16_t (*indexes)[8],
	uint16_t *offset) {
	uint16_t *idx = *indexes;
	*offset		  = 4;
	uint16_t end =
		net_buffer->tail - net_buffer->data - offsetof(DhcpHeader, options);
	while (header->options[*offset] != DHCP_OPTION_END && *offset < end) {
		uint8_t option = header->options[(*offset)++];
		uint8_t length = header->options[(*offset)++];
		switch (option) {
		case DHCP_OPTION_PAD:
			(*offset)--;
			break;
		case DHCP_OPTION_SUBNET_MASK:
			idx[DOI_SUBNET_MASK] = *offset;
			break;
		case DHCP_OPTION_ROUTER:
			idx[DOI_ROUTER] = *offset;
			break;
		case DHCP_OPTION_DOMAIN_NAME_SERVER:
			idx[DOI_DNS] = *offset;
			break;
		case DHCP_OPTION_IP_ADDRESS_LEASE_TIME:
			idx[DOI_IP_LEASE_TIME] = *offset;
			break;
		case DHCP_OPTION_MESSAGE_TYPE:
			idx[DOI_MESSAGE_TYPE] = *offset;
			break;
		case DHCP_OPTION_SERVER_IDENTIFIER:
			idx[DOI_SERVER_ID] = *offset;
			break;
		case DHCP_OPTION_RENEWAL_TIME:
			idx[DOI_RENEWAL_TIME] = *offset;
			break;
		case DHCP_OPTION_REBINDING_TIME:
			idx[DOI_REBINDING_TIME] = *offset;
			break;
		default:
			break;
		}
		*offset += length;
	}
}

void dhcp_check_addr(DhcpClient *dhcp) {
	NeighbourKey	key = ipv4_hash(dhcp->server_ip_addr);
	NeighbourEntry *entry =
		neighbour_table_lookup(dhcp->device, key, dhcp->server_ip_addr, 4);
	entry->ops->probe(dhcp->device);
	if (entry->state == NEIGH_STATE_REACHABLE) {
		entry->hlen = dhcp->haddr_len;
		memcpy(dhcp->server_haddr, entry->haddr, dhcp->haddr_len);
	}
}

void dhcp_rx_handler(NetworkConnection *conn, NetBuffer *net_buffer) {
	DhcpClient *dhcp   = (DhcpClient *)conn->udp.private_data;
	DhcpHeader *header = (DhcpHeader *)net_buffer->data;

	if (dhcp == NULL) goto end;
	if (net_buffer->tail - net_buffer->data < sizeof(DhcpHeader)) {
		print_warning("DHCP", "Received packet too small\n");
		goto end;
	}
	if (header->op != DHCP_OP_BOOTREPLY) {
		print_warning("DHCP", "Received packet is not a DHCP reply\n");
		goto end;
	}
	if (memcmp(dhcp->haddr, header->chaddr, dhcp->haddr_len) != 0) {
		print_warning("DHCP", "Received packet is not for this client\n");
		goto end;
	}
	if (BE2HOST_DWORD(header->xid) != dhcp->xid) {
		print_warning("DHCP", "Received packet is not for this transaction\n");
		goto end;
	}

	uint16_t indexes[DOI_MAX] = {0};
	uint16_t offset;
	dhcp_parse_options(net_buffer, header, &indexes, &offset);

	uint8_t msg_type = header->options[indexes[DOI_MESSAGE_TYPE]];
	if (msg_type == DHCP_ACK) {
		// 收到ACK，表示成功获取IP地址
		if (dhcp->state == DHCP_STAT_REQUESTING ||
			dhcp->state == DHCP_STAT_REBOOTING) {
			dhcp->state = DHCP_STAT_BOUND;
			memcpy(dhcp->ip_addr, &header->yiaddr, 4);
			memcpy(dhcp->server_ip_addr, &header->siaddr, 4);

			timer_callback_cancel(&dhcp->timeout_timer);
			printk(
				"[DHCP]Successfully bound to IP: %d.%d.%d.%d\n",
				dhcp->ip_addr[0], dhcp->ip_addr[1], dhcp->ip_addr[2],
				dhcp->ip_addr[3]);

			// TODO: 检查地址冲突
			dhcp_check_addr(dhcp);

			dhcp_ack_handler(dhcp, conn, header, indexes);
			dhcp_set_timers(dhcp);
		} else if (
			dhcp->state == DHCP_STAT_RENEWING ||
			dhcp->state == DHCP_STAT_REBINDING) {
			dhcp_ack_handler(dhcp, conn, header, indexes);
			dhcp_set_timers(dhcp);
		}
	} else if (msg_type == DHCP_NAK) {
		if (dhcp->state == DHCP_STAT_REQUESTING ||
			dhcp->state == DHCP_STAT_REBOOTING ||
			dhcp->state == DHCP_STAT_REBINDING ||
			dhcp->state == DHCP_STAT_RENEWING) {
			dhcp_nak_handler(dhcp, conn);
		}
	} else if (msg_type == DHCP_OFFER && dhcp->state == DHCP_STAT_SELECTING) {
		dhcp_offer_handler(dhcp, conn, header, indexes);
	}

end:
	kfree(net_buffer->ptr);
	kfree(net_buffer);
}

void *dhcp_create_message(DhcpClient *dhcp, DhcpHeader *header, uint8_t op) {
	net_buffer_reset(dhcp->conn->buffer);
	header->op	  = DHCP_OP_BOOTREQUEST;
	header->hlen  = dhcp->haddr_len;
	header->htype = DHCP_HTYPE_ETHERNET;
	header->hops  = 0;
	header->xid	  = HOST2BE_DWORD(dhcp->xid);

	header->ciaddr = *((uint32_t *)dhcp->ip_addr);
	header->yiaddr = 0;
	header->siaddr = 0;
	header->giaddr = 0;
	memcpy(header->chaddr, dhcp->haddr, dhcp->haddr_len);

	uint8_t *ptr	 = (uint8_t *)&header->options;
	*(uint32_t *)ptr = HOST2BE_DWORD(DHCP_MAGIC_COOKIE);
	ptr += 4;
	OPTION_SET_BASE(ptr, 1, DHCP_OPTION_MESSAGE_TYPE, op);

	return ptr;
}

ProtocolResult dhcp_send(DhcpClient *dhcp, uint16_t len) {
	ProtocolResult result = net_buffer_put(dhcp->conn->buffer, len);
	if (result != PROTO_OK) {
		printk("[DHCP] Failed to put DHCP message: %d\n", result);
		return result;
	}

	udp_wrap(
		dhcp->conn, UDP_PORT_DHCP_CLIENT,
		UDP_PORT_DHCP_SERVER); // DHCP客户端端口68，服务器端口67
	if (dhcp->state == DHCP_STAT_SELECTING ||
		dhcp->state == DHCP_STAT_REQUESTING ||
		dhcp->state == DHCP_STAT_REBINDING) {
		ipv4_wrap(
			dhcp->conn, IP_PROTO_UDP, (uint8_t *)&ipv4_broadcast_addr, 64);
	} else {
		ipv4_wrap(dhcp->conn, IP_PROTO_UDP, dhcp->server_ip_addr, 64);
	}
	switch (dhcp->device->type) {
	case NETWORK_TYPE_ETHERNET:
		eth_wrap(
			dhcp->conn->buffer, dhcp->device->ethernet->mac_addr,
			dhcp->server_haddr, ETH_PROTO_TYPE_IPV4);
		break;
	case NETWORK_TYPE_UNKNOWN:
		return PROTO_ERROR_UNSUPPORT;
	}

	TransferResult tresult = NETWORK_SEND(dhcp->device, dhcp->conn);
	if (tresult != TRANSFER_OK) {
		printk("[DHCP] Failed to send DHCP message: %d\n", tresult);
		return PROTO_ERROR_OTHER;
	}
	return PROTO_OK;
}

ProtocolResult dhcp_discover(DhcpClient *dhcp) {
	// 申请新地址
	dhcp->state = DHCP_STAT_SELECTING;

	*((uint32_t *)dhcp->ip_addr) = 0;
	uint8_t *ptr =
		dhcp_create_message(dhcp, dhcp->conn->buffer->data, DHCP_DISCOVER);

	OPTION_SET_HEADER(
		ptr, dhcp_discover_option_list_size,
		DHCP_OPTION_PARAMETER_REQUEST_LIST);
	for (int i = 0; i < dhcp_discover_option_list_size; i++, ptr++) {
		*ptr = dhcp_discover_option_list[i];
	}

	*ptr = DHCP_OPTION_END;

	int len = 4									 /* magic cookie */
			+ 3									 /* message type */
			+ 2 + dhcp_discover_option_list_size /* parameter request list */
			+ 1 /* end */;

	int initial_timeout = DHCP_INIT_TIMEOUT(dhcp);
	DHCP_SET_TIMEOUT(dhcp, initial_timeout);

	ProtocolResult result = dhcp_send(dhcp, sizeof(DhcpHeader) + len);
	if (result != PROTO_OK) return result;

	return PROTO_OK;
}

ProtocolResult dhcp_select(DhcpClient *dhcp) {
	// 选择IP地址
	dhcp->state = DHCP_STAT_REQUESTING;

	uint8_t *ptr =
		dhcp_create_message(dhcp, dhcp->conn->buffer->data, DHCP_REQUEST);

	// TODO: 设置Max Message Size

	// 设置请求的IP地址
	OPTION_SET_HEADER(ptr, 4, DHCP_OPTION_REQUESTED_IP_ADDRESS);
	*(uint32_t *)ptr = *((uint32_t *)dhcp->ip_addr);
	ptr += 4;

	// 设置服务器ID(IP地址)
	OPTION_SET_HEADER(ptr, 4, DHCP_OPTION_SERVER_IDENTIFIER);
	*(uint32_t *)ptr = *((uint32_t *)dhcp->server_ip_addr);
	ptr += 4;

	OPTION_SET_HEADER(
		ptr, dhcp_discover_option_list_size,
		DHCP_OPTION_PARAMETER_REQUEST_LIST);
	for (int i = 0; i < dhcp_discover_option_list_size; i++, ptr++) {
		*ptr = dhcp_discover_option_list[i];
	}

	*ptr = DHCP_OPTION_END;

	int len = 4									 /* magic cookie */
			+ 3									 /* message type */
			+ 6									 /* requested ip address */
			+ 6									 /* server identifier */
			+ 2 + dhcp_discover_option_list_size /* parameter request list */
			+ 1 /* end */;

	int initial_timeout = DHCP_INIT_TIMEOUT(dhcp);
	DHCP_SET_TIMEOUT(dhcp, initial_timeout);

	ProtocolResult result = dhcp_send(dhcp, sizeof(DhcpHeader) + len);
	if (result != PROTO_OK) return result;

	return PROTO_OK;
}

void dhcp_init(DhcpClient *dhcp, NetworkConnection *conn) {
	dhcp->conn			   = conn;
	conn->udp.private_data = dhcp;

	timer_init(&dhcp->timeout_timer);
	timer_init(&dhcp->lease_timer);
	timer_init(&dhcp->renew_timer);
	timer_init(&dhcp->rebind_timer);
	dhcp->timeout_timer.callback = dhcp_timeout_handler;
	dhcp->lease_timer.callback	 = dhcp_ip_lease_handler;
	dhcp->renew_timer.callback	 = dhcp_renew_handler;
	dhcp->rebind_timer.callback	 = dhcp_rebind_handler;
	dhcp->timeout_timer.arg		 = dhcp;
	dhcp->lease_timer.arg		 = dhcp;
	dhcp->renew_timer.arg		 = dhcp;
	dhcp->rebind_timer.arg		 = dhcp;
}

void dhcp_reset(DhcpClient *dhcp, NetworkConnection *conn) {
	dhcp->state = DHCP_STAT_INIT;
	dhcp->xid	= rand();

	memset(dhcp->ip_addr, 0, sizeof(dhcp->ip_addr));
	memset(dhcp->server_ip_addr, 0, sizeof(dhcp->server_ip_addr));
	memset(dhcp->server_haddr, 0, sizeof(dhcp->server_haddr));
	net_buffer_init(conn->buffer, 576, 0, 0);
	eth_register(conn);
	ipv4_register(conn, NULL);
	udp_register(conn);

	udp_bind(dhcp->conn, UDP_PORT_DHCP_CLIENT);
	udp_set_callback(conn, dhcp_rx_handler);

	switch (dhcp->device->type) {
	case NETWORK_TYPE_ETHERNET:
		dhcp->haddr_len	 = ETH_IDENTIFIER_SIZE;
		dhcp->haddr_type = DHCP_HTYPE_ETHERNET;
		memcpy(dhcp->haddr, dhcp->device->ethernet->mac_addr, dhcp->haddr_len);
		memcpy(dhcp->server_haddr, eth_broadcast_mac, dhcp->haddr_len);
		break;
	default:
		break;
	}
	*(uint32_t *)dhcp->server_ip_addr = ipv4_broadcast_addr;
}

ProtocolResult dhcp_start(NetworkDevice *device) {
	ProtocolResult result;

	DhcpClient *dhcp = kmalloc(sizeof(DhcpClient));

	dhcp->device = device;

	NetworkConnection *conn = net_create_conn(device->device->object);
	conn->buffer			= net_buffer_create(576);

	dhcp_init(dhcp, conn);

	dhcp_reset(dhcp, conn);

	result = dhcp_discover(dhcp);

	return result;
}