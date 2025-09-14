#include "driver/network/network.h"
#include "driver/network/buffer.h"
#include "driver/network/conn.h"
#include "driver/network/protocols/protocols.h"
#include "kernel/list.h"
#include <bits.h>
#include <driver/network/net_queue.h>
#include <driver/network/network_dm.h>
#include <kernel/driver.h>
#include <objects/handle.h>
#include <objects/object.h>
#include <objects/transfer.h>

LIST_HEAD(net_rx_lh);

void net_queue_block(NetworkQueue *queue, int blocker) {
	queue->blocker = blocker;
}

bool net_queue_is_blocked(NetworkQueue *queue, int blocker) {
	return (queue->blocker & blocker) == blocker;
}

TransferResult network_transfer(
	struct Object *object, struct ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, size_t size) {
	Device		  *device	  = object->value.device;
	NetworkDevice *net_device = device->dm_ext;
	if (net_device->ops->send == NULL) { return TRANSFER_ERROR_NOT_SUPPORTED; }
	if (net_queue_is_blocked(&net_device->tx_queue, NQ_BLOCKER_DRIVER)) {
		return TRANSFER_ERROR_FAILED;
	}
	return net_device->ops->send(net_device, buf, size);
}

DriverResult network_softirq_register(NetRxHandler *handler) {
	list_add_tail(&handler->list, &net_rx_lh);
	return DRIVER_RESULT_OK;
}

void network_softirq_handler(void) {
	NetRxHandler *handler;
	list_for_each_owner (handler, &net_rx_lh, list) {
		if (handler->handler) {
			handler->handler(handler->data);
		} else {
			printk("[Network] Rx handler is NULL, skipping.\n");
		}
	}
}

NetBuffer *network_recv(NetworkConnection *conn) {
	NetBuffer *net_buffer = NULL;
	if (!list_empty(&conn->recv_lh)) {
		disable_preempt();
		spin_lock(&conn->recv_lock);
		net_buffer = list_first_owner(&conn->recv_lh, NetBuffer, list);
		list_del(&net_buffer->list);
		spin_unlock(&conn->recv_lock);
		enable_preempt();
	}
	return net_buffer;
}

ProtocolResult protocol_recv(
	NetworkDevice *device, NetBuffer *net_buffer, NetworkDeviceType type) {
	ProtocolReplyCallback callback_stack[NET_CONN_MAX_PROTOCOLS];
	switch (type) {
	case NETWORK_TYPE_ETHERNET:
		return eth_recv(
			device, net_buffer, callback_stack, NET_CONN_MAX_PROTOCOLS);
	default:
		return PROTO_ERROR_UNSUPPORT;
	}
}

ProtocolResult protocol_reply(
	NetworkDevice *device, NetBuffer *net_buffer, ProtocolReplyCallback *stack,
	int stack_size) {
	ProtocolResult result = PROTO_OK;
	while (stack_size++ < NET_CONN_MAX_PROTOCOLS) {
		result = (*--stack)(device, net_buffer);
		if (result != PROTO_OK) { return result; }
	}
	device->ops->send(
		device, net_buffer->head, net_buffer->tail - net_buffer->head);
	return result;
}
