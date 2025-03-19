#ifndef _STORAGE_DM_H
#define _STORAGE_DM_H

#include "kernel/device_manager.h"
#include "kernel/driver.h"
#include "kernel/list.h"
#include "kernel/periodic_task.h"
#include "kernel/spinlock.h"
#include "objects/object.h"
#include "string.h"
#include <stdint.h>

typedef enum StorageDeviceType {
	STORAGE_DEVICE_TYPE_UNKNOWN,
	STORAGE_DEVICE_TYPE_HARDDISK,
} StorageDeviceType;

struct StorageDevice;
struct StorageRequest;
typedef struct StorageDeviceOps {
	DriverResult (*submit_read_request)(
		struct StorageDevice *storage_device, struct StorageRequest *request);
	DriverResult (*submit_write_request)(
		struct StorageDevice *storage_device, struct StorageRequest *request);
	bool (*is_busy)(struct StorageDevice *storage_device);
} StorageDeviceOps;

#define SECTOR_SIZE 512

struct Object;
typedef struct StorageDevice {
	Device			 *device;
	StorageDeviceType type;
	StorageDeviceOps *ops;

	string_t name;

	uint32_t block_size;
	size_t	 max_block_per_request;

	spinlock_t	 queue_lock;
	PeriodicTask periodic_task;
	list_t		 io_queue_lh;

	uint8_t *superblock;

	list_t block_cache_lh;

	// 存储设备的分区目录对象
	struct Object *object;
} StorageDevice;

typedef struct StorageDeviceDriver {
	string_t		  name;
	list_t			  driver_list;
	list_t			  device_list;
	StorageDeviceOps *ops;
} StorageDeviceDriver;

extern DeviceManager storage_dm;

DriverResult register_storage_device(
	struct DeviceDriver *device_driver, Device *device,
	StorageDevice *storage_device, ObjectAttr *attr);
DriverResult unregister_storage_device(
	struct DeviceDriver *device_driver, Device *device,
	StorageDevice *storage_device);

#endif