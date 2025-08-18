#include "kernel/list.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include "objects/object.h"
#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <kernel/memory.h>
#include <objects/handle.h>

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
