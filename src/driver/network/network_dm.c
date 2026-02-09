#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <driver/network/ethernet/ethernet.h>
#include <driver/network/neighbour.h>
#include <driver/network/network.h>
#include <driver/network/network_dm.h>
#include <driver/timer/timer_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/softirq.h>
#include <objects/object.h>
#include <objects/transfer.h>

DriverResult network_dm_load(DeviceManager *manager);
DriverResult network_dm_unload(DeviceManager *manager);

DeviceManagerOps network_dm_ops = {
	.dm_load   = network_dm_load,
	.dm_unload = network_dm_unload,
};

NetworkDeviceManager network_dm_ext;
DeviceManager		 network_dm = {
		   .type		 = DEVICE_TYPE_INTERNET,
		   .ops			 = &network_dm_ops,
		   .private_data = &network_dm_ext,
};

DriverResult network_dm_load(DeviceManager *manager) {
	manager->private_data = kzalloc(sizeof(NetworkDeviceManager));
	softirq_register_handler(SOFTIRQ_NETWORK, network_softirq_handler);
	neighbour_init();
	return DRIVER_OK;
}

DriverResult network_dm_unload(DeviceManager *manager) {
	kfree(manager->private_data);
	return DRIVER_OK;
}

DriverResult create_network_device(
	NetworkDevice **network_device, NetworkDeviceType type,
	NetworkDeviceCapabilities caps, NetworkOps *net_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_INTERNET);
	if (result != DRIVER_OK) return result;

	*network_device = kzalloc(sizeof(NetworkDevice));
	if (*network_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}

	NetworkDevice *net	   = *network_device;
	logical_device->dm_ext = net;
	net->type			   = type;
	net->capabilities	   = caps;
	net->ops			   = net_ops;
	net->state			   = NET_STATE_INITED;
	net->device			   = logical_device;
	net->tx_queue.blocker  = 0;

	char	 _name[] = "Network";
	string_t name;
	string_new_with_number(
		&name, _name, sizeof(_name) - 1, network_dm_ext.device_count);
	network_dm_ext.new_device_num++;
	network_dm_ext.device_count++;

	Object *obj = create_object(&device_object, &name, device_object_attr);
	if (obj == NULL) {
		kfree(net);
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OBJECT;
	}
	obj->value.device.kind	  = DEVICE_KIND_LOGICAL;
	obj->value.device.logical = logical_device;
	obj->in.type			  = TRANSFER_TYPE_NONE;
	obj->out.type			  = TRANSFER_TYPE_STREAM;
	obj->out.stream			  = network_transfer;
	logical_device->object	  = obj;

	switch (type) {
	case NETWORK_TYPE_ETHERNET: {
		EthernetDevice *eth = kzalloc(sizeof(EthernetDevice));
		net->ethernet		= eth;
		eth->acd_state		= ACD_STATE_NONE;
		eth->arp_conn		= net_create_conn(obj);

		conn_buffer(eth->arp_conn) = net_buffer_create(128);

		net_buffer_init(conn_buffer(eth->arp_conn), 128, 0, 0);
		timer_init(&eth->timer);
		eth_register(eth->arp_conn);
		break;
	}
	default:
		break;
	}

	return DRIVER_OK;
}

DriverResult delete_network_device(NetworkDevice *network_device) {
	network_dm_ext.device_count--;
	LogicalDevice *logical_device = network_device->device;

	logical_device->object->out.type   = TRANSFER_TYPE_NONE;
	logical_device->object->out.stream = NULL;
	if (network_device->type == NETWORK_TYPE_ETHERNET) {
		net_buffer_delete(conn_buffer(network_device->ethernet->arp_conn));
		kfree(network_device->ethernet);
	}

	delete_logical_device(logical_device);
	int result = kfree(network_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

NetworkDevice *network_get_device(Object *object) {
	// if (object->attr->type != OBJECT_TYPE_DEVICE) return NULL;
	LogicalDevice *device = object->value.device.logical;
	// if (device->device_driver->type != DEVICE_TYPE_SOUND) return NULL;
	return device->dm_ext;
}

NetworkDeviceType network_get_type(Object *object) {
	NetworkDevice *network_device = network_get_device(object);
	if (network_device == NULL) return NETWORK_TYPE_UNKNOWN;
	return network_device->type;
}
