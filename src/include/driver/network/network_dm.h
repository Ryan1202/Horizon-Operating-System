#ifndef _NETWORK_DM_H
#define _NETWORK_DM_H

#include <driver/network/net_queue.h>
#include <driver/network/protocols/protocols.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <stdint.h>

#define NETWORK_SEND(device, conn) \
	((device)->ops->send(          \
		device, conn->buffer->head, conn->buffer->tail - conn->buffer->head))

struct NetworkDevice;
typedef struct NetworkDeviceOps {
	TransferResult (*send)(struct NetworkDevice *device, void *buf, int length);
} NetworkOps;

typedef struct NetworkDeviceCapabilities {
} NetworkDeviceCapabilities;

typedef enum NetworkDeviceType {
	NETWORK_TYPE_UNKNOWN,
	NETWORK_TYPE_ETHERNET,
} NetworkDeviceType;

typedef enum NetworkDeviceState {
	NET_STATE_INITED,
	NET_STATE_NO_CARRIER,
	NET_STATE_RUNNING,
} NetworkDeviceState;

typedef struct NetworkDevice {
	LogicalDevice			 *device;
	NetworkDeviceType		  type;
	NetworkDeviceCapabilities capabilities;
	NetworkOps				 *ops;

	int mtu;

	NetworkDeviceState state;
	NetworkQueue	   tx_queue;

	union {
		struct EthernetDevice *ethernet;
	};

	union {
		struct {
			uint8_t ip[4];

			uint8_t subnet_mask[4];
			uint8_t gateway_ip[4];
		} ipv4;
	};
} NetworkDevice;

typedef struct NetworkDeviceManager {
	int new_device_num;
	int device_count;
} NetworkDeviceManager;

extern DeviceManager network_dm;

DriverResult create_network_device(
	NetworkDevice **network_device, NetworkDeviceType type,
	NetworkDeviceCapabilities caps, NetworkOps *net_ops, DeviceOps *ops,
	PhysicalDevice *device, DeviceDriver *device_driver);
DriverResult delete_network_device(NetworkDevice *network_device);

NetworkDevice *network_get_device(Object *object);

struct NetworkConnection;
void		   net_queue_block(NetworkQueue *queue, int blocker);
bool		   net_queue_is_blocked(NetworkQueue *queue, int blocker);
TransferResult network_transfer(
	struct Object *object, struct ObjectHandle *obj_handle,
	TransferDirection direction, uint8_t *buf, size_t size);
struct NetBuffer *network_recv(struct NetworkConnection *conn);
ProtocolResult	  protocol_recv(
	   NetworkDevice *device, struct NetBuffer *net_buffer,
	   NetworkDeviceType type);

#endif