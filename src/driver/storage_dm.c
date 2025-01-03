#include <driver/storage_dm.h>
#include <driver/storage_io_queue.h>
#include <driver/transfer.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/periodic_task.h>
#include <math.h>
#include <result.h>
#include <stddef.h>
#include <types.h>

extern void storage_periodic_task(void *arg);

DriverResult storage_device_block_read(
	Device *device, uint8_t *buf, offset_t offset, size_t size);
DriverResult storage_device_block_write(
	Device *device, uint8_t *buf, offset_t offset, size_t size);

DeviceManagerOps storage_dm_ops = {
	.dm_load_hook	= NULL,
	.dm_unload_hook = NULL,
};

typedef struct StorageDeviceManager {
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

	if (device->transfer->type_in == TRANSFER_TYPE_BLOCK) {
		device->transfer->in.block = storage_device_block_read;
	}
	if (device->transfer->type_out == TRANSFER_TYPE_BLOCK) {
		device->transfer->out.block = storage_device_block_write;
	}
	device->device_manager_extension = storage_device;
	list_init(&storage_device->io_queue_lh);

	DRV_RESULT_DELIVER_CALL(
		register_device, device_driver, device_driver->bus, device);
	list_add_tail(&device->dm_list, &storage_device_manager.device_lh);

	storage_device->periodic_task.func = storage_periodic_task;
	storage_device->periodic_task.arg  = storage_device;
	periodic_task_add(&storage_device->periodic_task);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_storage_device(
	DeviceDriver *device_driver, Device *device,
	StorageDevice *storage_device) {

	DRV_RESULT_DELIVER_CALL(unregister_device, device_driver, device);
	list_del(&device->device_list);
	return DRIVER_RESULT_OK;
}

DriverResult storage_device_block_read(
	Device *device, uint8_t *buf, offset_t offset, size_t size) {

	StorageRequest *request = kmalloc(sizeof(StorageRequest));
	request->rw				= false;
	request->buf			= buf;
	request->offset			= offset % 512;
	request->position		= offset / 512;
	request->count = DIV_ROUND_UP(offset + size, 512) - request->position;

	storage_add_request(device->device_manager_extension, request);

	return DRIVER_RESULT_OK;
}

DriverResult storage_device_block_write(
	Device *device, uint8_t *buf, offset_t offset, size_t size) {

	StorageRequest *request = kmalloc(sizeof(StorageRequest));
	request->rw				= true;
	request->buf			= buf;
	request->offset			= offset % 512;
	request->position		= offset / 512;
	request->count = DIV_ROUND_UP(offset + size, 512) - request->position;

	storage_add_request(device->device_manager_extension, request);

	return DRIVER_RESULT_OK;
}
