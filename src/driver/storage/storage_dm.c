#include <driver/storage/disk/mbr.h>
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io.h>
#include <driver/storage/storage_io_queue.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/periodic_task.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <result.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

extern void	 storage_periodic_task(void *arg);
DriverResult start_storage_device(DeviceManager *manager, Device *device);

DeviceManagerOps storage_dm_ops = {
	.dm_load_hook	   = NULL,
	.dm_unload_hook	   = NULL,
	.start_device_hook = start_storage_device,
	.stop_device_hook  = NULL,
};

typedef struct StorageDeviceManager {
	uint8_t device_count;
} StorageDeviceManager;

StorageDeviceManager storage_dm_ext;

struct DeviceManager storage_device_manager = {
	.type = DEVICE_TYPE_STORAGE,

	.ops = &storage_dm_ops,

	.private_data = &storage_dm_ext,
};

DriverResult register_storage_device(
	DeviceDriver *device_driver, Device *device,
	StorageDevice *storage_device) {
	storage_device->device = device;

	device->device_manager_extension = storage_device;
	list_init(&storage_device->io_queue_lh);

	string_t name;
	string_new_with_number(&name, "Storage", 7, storage_dm_ext.device_count++);
	DRV_RESULT_DELIVER_CALL(
		register_device, device_driver, name, device->bus, device);
	list_add_tail(&device->dm_list, &storage_device_manager.device_lh);

	device->object->in.block			 = storage_transfer;
	device->object->in.is_transfer_done	 = storage_is_transfer_done;
	device->object->out.block			 = storage_transfer;
	device->object->out.is_transfer_done = storage_is_transfer_done;

	storage_device->periodic_task.func = storage_periodic_task;
	storage_device->periodic_task.arg  = storage_device;
	periodic_task_add(&storage_device->periodic_task);

	storage_device->object = create_object_directory(&device_object, name);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_storage_device(
	DeviceDriver *device_driver, Device *device,
	StorageDevice *storage_device) {

	DRV_RESULT_DELIVER_CALL(unregister_device, device_driver, device);
	list_del(&device->device_list);
	return DRIVER_RESULT_OK;
}

DriverResult start_storage_device(DeviceManager *manager, Device *device) {
	StorageDevice *storage_device = device->device_manager_extension;
	void		  *handle;
	storage_device->superblock = kmalloc(2 * 512);
	storage_transfer(
		device->object, TRANSFER_IN, storage_device->superblock, 0, 2, &handle);

	bool is_done;
	do {
		storage_is_transfer_done(device->object, &handle, &is_done);
	} while (!is_done);

	if (storage_device->type == STORAGE_DEVICE_TYPE_HARDDISK) {
		if (disk_is_mbr(storage_device)) {
			parse_mbr_partition_table(storage_device);
		}
	}
	return DRIVER_RESULT_OK;
}
