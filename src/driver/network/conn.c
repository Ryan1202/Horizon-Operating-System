#include "driver/network/ethernet/ethernet.h"
#include "driver/network/protocols/ipv4/ipv4.h"
#include "driver/network/protocols/protocols.h"
#include "kernel/list.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include "objects/object.h"
#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <kernel/memory.h>
#include <objects/handle.h>
#include <stdint.h>

NetworkConnection *net_create_conn(Object *object) {
	if (object->attr->type != OBJECT_TYPE_DEVICE) { return NULL; }
	NetworkConnection *conn = kmalloc(sizeof(NetworkConnection));
	if (conn == NULL) { return NULL; }
	conn->object	 = object;
	conn->handle	 = object_handle_create(object);
	conn->net_device = object->value.device->dm_ext;

	conn->thread = get_current_thread();

	conn->phy_protocol	 = PHY_PROTO_NONE;
	conn->dl_protocol	 = DL_PROTO_NONE;
	conn->net_protocol	 = NET_PROTO_NONE;
	conn->trans_protocol = TRANS_PROTO_NONE;
	conn->tcp.info		 = NULL;

	spinlock_init(&conn->recv_lock);
	list_init(&conn->recv_lh);
	return conn;
}

void net_destroy_conn(NetworkConnection *conn) {
	// NetProtocol *protocol = conn->protocols;
	// while (protocol != NULL) {
	// 	NetProtocol *next = protocol->next;
	// 	protocol->ops.destroy(conn, protocol->context);
	// 	kfree(protocol);
	// 	protocol = next;
	// }
	object_handle_delete(conn->handle);
	kfree(conn);
}

ProtocolResult conn_wrap(NetworkConnection *conn, ProtocolLevel level) {
	if (conn == NULL) { return PROTO_ERROR_NULL_PTR; }
	uint16_t trans_protocol = 0;
	uint16_t net_protocol	= 0;
	uint8_t	 dst_mac[8]		= {0};

	ProtocolResult result = PROTO_OK;
	switch (level) {
	case PROTO_LEVEL_TRANSPORT:
	case PROTO_LEVEL_NETWORK:
		if (trans_protocol == 0) {
			switch (conn->trans_protocol) {
			case TRANS_PROTO_UDP:
				trans_protocol = IP_PROTO_UDP;
				break;
			case TRANS_PROTO_TCP:
				trans_protocol = IP_PROTO_TCP;
				break;
			default:
				break;
			}
		}
		switch (conn->net_protocol) {
		case NET_PROTO_IPV4:
			result = ipv4_wrap(conn, trans_protocol, CONN_REMOTE_IP(conn), 64);
			if (result != PROTO_OK) return result;
			result = ipv4_lookup_mac(
				conn->net_device, conn->ipv4.conn_info.remote.ip, dst_mac);
			break;
		default:
			return PROTO_ERROR_UNSUPPORT;
		}
	case PROTO_LEVEL_DATA_LINK:
		switch (conn->dl_protocol) {
		default:
			break;
		}
	case PROTO_LEVEL_PHYSICAL:
		if (net_protocol == 0) {
			switch (conn->net_protocol) {
			case NET_PROTO_IPV4:
				net_protocol = ETH_PROTO_TYPE_IPV4;
				break;
			default:
				return PROTO_ERROR_UNSUPPORT;
			}
		}
		if (net_protocol == 0) {
			switch (conn->dl_protocol) {
			case DL_PROTO_ARP:
				net_protocol = ETH_PROTO_TYPE_ARP;
				break;
			default:
				return PROTO_ERROR_UNSUPPORT;
			}
		}
		switch (conn->phy_protocol) {
		case PHY_PROTO_ETHERNET:
			result = eth_wrap(
				conn->buffer, conn->ethernet.mac, dst_mac, net_protocol);
			break;
		default:
			return PROTO_ERROR_UNSUPPORT;
		}
		break;
	}

	return result;
}
