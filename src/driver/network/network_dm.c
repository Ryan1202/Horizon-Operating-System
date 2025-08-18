#include "driver/network/network.h"
#include "kernel/softirq.h"
#include "objects/transfer.h"
#include <driver/network/network_dm.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>

DriverResult network_dm_load(DeviceManager *manager);
DriverResult network_dm_unload(DeviceManager *manager);

DeviceManagerOps network_dm_ops = {
	.dm_load   = network_dm_load,
	.dm_unload = network_dm_unload,
};

NetworkDeviceManager network_dm_ext;
DeviceManager		 network_dm = {
		   .type		 = DEVICE_TYPE_ETHERNET,
		   .ops			 = &network_dm_ops,
		   .private_data = &network_dm_ext,
};

DriverResult network_dm_load(DeviceManager *manager) {
	manager->private_data = kmalloc(sizeof(NetworkDeviceManager));
	softirq_register_handler(SOFTIRQ_NETWORK, network_softirq_handler);
	return DRIVER_RESULT_OK;
}

DriverResult network_dm_unload(DeviceManager *manager) {
	kfree(manager->private_data);
	return DRIVER_RESULT_OK;
}

DriverResult register_network_device(
	DeviceDriver *driver, Device *device, NetworkDevice *network_device,
	ObjectAttr *attr) {
	device->dm_ext					 = network_device;
	network_device->device			 = device;
	network_device->private_data	 = device->private_data;
	network_device->state			 = NET_STATE_INITED;
	network_device->tx_queue.blocker = 0;
	list_add_tail(&device->dm_list, &network_dm.device_lh);

	string_t name;
	string_new_with_number(&name, "Network", 7, network_dm_ext.device_count++);
	DRIVER_RESULT_PASS(register_device(
		device->device_driver, name, device->bus, device, attr));

	device->object->out.type   = TRANSFER_TYPE_STREAM;
	device->object->out.stream = network_transfer;

	return DRIVER_RESULT_OK;
}

NetworkDevice *network_get_device(Object *object) {
	if (object->attr->type != OBJECT_TYPE_DEVICE) return NULL;
	Device *device = object->value.device;
	if (device->device_driver->type != DEVICE_TYPE_SOUND) return NULL;
	return device->dm_ext;
}

NetworkDeviceType network_get_type(Object *object) {
	NetworkDevice *network_device = network_get_device(object);
	if (network_device == NULL) return NETWORK_TYPE_UNKNOWN;
	return network_device->type;
}
