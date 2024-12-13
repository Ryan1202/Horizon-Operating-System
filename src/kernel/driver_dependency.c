#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/list.h>
#include <kernel/thread.h>
#include <kernel/wait_queue.h>

DriverResult check_dependency(Driver *driver) {
	int				  count = driver->dependency_count;
	DriverDependency *deps	= driver->dependencies;

	for (int i = 0; i < count; i++) {
		if (deps[i].in_type == DRIVER_DEPENDENCY_TYPE_BUS) {
			if (deps[i].dependency_in_bus.type > BUS_TYPE_MAX) {
				return DRIVER_RESULT_BUS_DRIVER_NOT_EXIST;
			}

			Bus		  *bus;
			BusDriver *bus_driver = bus_drivers[deps[i].dependency_in_bus.type];
			if (bus_driver == NULL ||
				bus_driver->subdriver.state != SUBDRIVER_STATE_READY) {
				// 总线驱动还没准备好则等待
				wait_queue_add(&bus_driver->subdriver.wqm, 0);
				thread_block(TASK_BLOCKED);
				bus_driver = bus_drivers[deps[i].dependency_in_bus.type];
			}

			int j = 0;
			list_for_each_owner (bus, &bus_driver->bus_lh, bus_list) {
				if (j == deps[i].dependency_in_bus.bus_num) {
					deps[i].out_bus = bus;
					break;
				}
				j++;
			}
		} else if (deps[i].in_type == DRIVER_DEPENDENCY_TYPE_DEVICE) {
			if (deps[i].in_device_type > DEVICE_TYPE_MAX) {
				return DRIVER_RESULT_DEVICE_MANAGER_NOT_EXIST;
			}
			// TODO: device依赖
		}
	}
	return DRIVER_RESULT_OK;
}