#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_manager.h>
#include <kernel/list.h>
#include <result.h>

DriverManager *driver_managers[DRIVER_TYPE_MAX];

DriverResult register_driver_manager(DriverManager *driver_manager) {

	DriverManager *old_manager = driver_managers[driver_manager->type];
	if (old_manager != NULL) {
		DRV_RESULT_DELIVER_CALL(unregister_driver_manager, old_manager);
	}

	list_init(&driver_manager->dm_lh);
	driver_managers[driver_manager->type] = driver_manager;

	DM_OPS_CALL(driver_manager, dm_load_hook, driver_manager);
	return DRIVER_RESULT_OK;
}

DriverResult unregister_driver_manager(DriverManager *driver_manager) {
	DriverManager *old_manager = driver_managers[driver_manager->type];

	if (old_manager == NULL) return DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST;
	DM_OPS_CALL(old_manager, dm_unload_hook, old_manager);

	driver_managers[driver_manager->type] = NULL;

	return DRIVER_RESULT_OK;
}
