#ifndef _STORAGE_DM_H
#define _STORAGE_DM_H

#include "kernel/device_driver.h"
#include "kernel/device_manager.h"
#include "kernel/driver.h"
#include "kernel/periodic_task.h"

typedef enum StorageDeviceType {
	STORAGE_DEVICE_TYPE_UNKNOWN,
	STORAGE_DEVICE_TYPE_ATA,
	STORAGE_DEVICE_TYPE_SCSI,
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

typedef struct StorageDevice {
	Device			 *device;
	StorageDeviceType type;
	StorageDeviceOps *ops;

	PeriodicTask periodic_task;
	list_t		 io_queue_lh;
} StorageDevice;

typedef struct StorageDeviceDriver {
	string_t		  name;
	list_t			  driver_list;
	list_t			  device_list;
	StorageDeviceOps *ops;
} StorageDeviceDriver;

extern DeviceManager storage_device_manager;

DriverResult register_storage_device(
	DeviceDriver *device_driver, Device *device, StorageDevice *storage_device);
DriverResult unregister_storage_device(
	DeviceDriver *device_driver, Device *device, StorageDevice *storage_device);

#endif