#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <result.h>

DeviceManager *device_managers[DEVICE_TYPE_MAX];

DriverResult register_device_manager(DeviceManager *manager) {
	DeviceManager *old_manager = device_managers[manager->type];
	if (old_manager != NULL) {
		DRV_RESULT_DELIVER_CALL(unregister_device_manager, old_manager);
	}

	device_managers[manager->type] = manager;

	DEVM_OPS_CALL(manager, dm_load_hook, manager);
	return DRIVER_RESULT_OK;
}

DriverResult unregister_device_manager(DeviceManager *manager) {
	// 关闭所有设备
	Device *cur;
	list_for_each_owner (cur, &manager->device_driver_lh, device_list) {
		if (cur->state != DEVICE_STATE_UNREGISTERED) {
			DEV_OPS_CALL(cur, destroy, cur);
		}
	}
	DEVM_OPS_CALL(manager, dm_unload_hook, manager);
	// 从链表中删除
	list_del(&manager->dm_list);

	device_managers[manager->type] = NULL;
	return DRIVER_RESULT_OK;
}
