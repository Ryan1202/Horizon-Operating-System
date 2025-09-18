#include <kernel/device.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/dynamic_device_manager.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/thread.h>

LIST_HEAD(dynamic_device_manager_lh);
LIST_HEAD(new_device_lh);

DriverResult register_dynamic_device_manager(
	DynamicDeviceEvents *events, DeviceManager *device_manager) {
	DynamicDeviceManager *ddm = kmalloc(sizeof(DynamicDeviceManager));
	ddm->device_manager		  = device_manager;
	ddm->events				  = events;
	list_init(&ddm->dynamic_device_lh);
	list_add_tail(&ddm->list, &dynamic_device_manager_lh);

	return DRIVER_RESULT_OK;
}

void dynamic_device_manager(void *arg) {
	DynamicDeviceManager *ddm;
	Device				 *device, *next;
	while (true) {
		list_for_each_owner (ddm, &dynamic_device_manager_lh, list) {
			ddm->events->probe(ddm->device_manager);
		}
		list_for_each_owner_safe (
			device, next, &new_device_lh, new_device_list) {
			list_del(&device->new_device_list);
			init_and_start(device);
		}
		schedule();
	}
}
