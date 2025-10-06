#include "kernel/spinlock.h"
#include "kernel/wait_queue.h"
#include "objects/attr.h"
#include <driver/storage/disk/mbr.h>
#include <driver/storage/disk/volume.h>
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
#include <stdint.h>
#include <string.h>
#include <types.h>

extern void	 storage_periodic_task(void *arg);
DriverResult start_storage_device(
	DeviceManager *manager, LogicalDevice *device);

DeviceManagerOps storage_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = start_storage_device,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

typedef struct StorageDeviceManager {
	uint8_t new_device_num;
	uint8_t device_count;
} StorageDeviceManager;

StorageDeviceManager storage_dm_ext;
struct DeviceManager storage_dm = {
	.type		  = DEVICE_TYPE_STORAGE,
	.ops		  = &storage_dm_ops,
	.private_data = &storage_dm_ext,
};

DriverResult create_storage_device(
	StorageDevice **storage_device, StorageDeviceOps *storage_ops,
	DeviceOps *ops, PhysicalDevice *physical_device,
	DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_STORAGE);
	if (result != DRIVER_OK) return result;

	*storage_device = kmalloc(sizeof(StorageDevice));
	if (*storage_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}

	StorageDevice *storage = *storage_device;
	logical_device->dm_ext = storage;
	storage->device		   = logical_device;
	storage->ops		   = storage_ops;

	spinlock_init(&storage->queue_lock);
	list_init(&storage->io_queue_lh);
	wait_queue_init(&storage->wq);

	char	 _name[] = "Storage";
	string_t name;
	string_new_with_number(
		&name, _name, sizeof(_name) - 1, storage_dm_ext.new_device_num);
	storage_dm_ext.new_device_num++;
	storage_dm_ext.device_count++;

	Object *obj = create_object(&device_object, &name, device_object_attr);
	logical_device->object = obj;
	if (logical_device->object == NULL) {
		kfree(storage);
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OBJECT;
	}

	obj->in.type			  = TRANSFER_TYPE_BLOCK;
	obj->in.block			  = storage_transfer;
	obj->in.block_async		  = storage_transfer_async;
	obj->in.is_transfer_done  = storage_is_transfer_done;
	obj->out.type			  = TRANSFER_TYPE_BLOCK;
	obj->out.block			  = storage_transfer;
	obj->out.block_async	  = storage_transfer_async;
	obj->out.is_transfer_done = storage_is_transfer_done;
	obj->value.device.kind	  = DEVICE_KIND_LOGICAL;
	obj->value.device.logical = logical_device;

	storage->periodic_task.func = storage_periodic_task;
	storage->periodic_task.arg	= storage;
	storage->name				= name;
	periodic_task_add(&storage->periodic_task);

	ObjectAttr attr = base_obj_sys_attr;
	attr.type		= OBJECT_TYPE_DIRECTORY;
	storage->object = create_object_directory(&device_object, &name, attr);

	return DRIVER_OK;
}

DriverResult delete_storage_device(StorageDevice *storage_device) {
	// TODO: delete_object_directory
	// TODO: periodic_task_remove

	list_del(&storage_device->device->dm_device_list);
	delete_logical_device(storage_device->device);
	int result;
	if (storage_device->device->state == DEVICE_STATE_ACTIVE) {
		result = kfree(storage_device->superblock);
		if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	}
	result = kfree(storage_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

DriverResult start_storage_device(
	DeviceManager *manager, LogicalDevice *device) {
	StorageDevice *storage_device = device->dm_ext;

	storage_device->superblock = kmalloc(2 * SECTOR_SIZE);
	storage_transfer(
		device->object, NULL, TRANSFER_IN, storage_device->superblock, 0, 2);

	if (storage_device->type == STORAGE_DEVICE_TYPE_HARDDISK) {
		if (disk_is_mbr(storage_device)) {
			parse_mbr_partition_table(storage_device);
		}
		list_init(&storage_device->block_cache_lh);
	} else {
		storage_device->block_cache_lh.next = NULL;
		storage_device->block_cache_lh.prev = NULL;
	}
	return DRIVER_OK;
}
