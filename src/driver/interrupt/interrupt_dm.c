#include <driver/interrupt/interrupt_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <result.h>
#include <stdint.h>
#include <string.h>

DriverResult interrupt_start(DeviceManager *manager, PhysicalDevice *device);

DeviceManagerOps interrupt_dm_ops = {
	.dm_load   = NULL,
	.dm_unload = NULL,

	.init_device_hook	 = NULL,
	.start_device_hook	 = NULL,
	.stop_device_hook	 = NULL,
	.destroy_device_hook = NULL,
};

typedef struct InterruptDeviceManager {
	InterruptDevice *current_device;
} InterruptDeviceManager;

InterruptDeviceManager interrupt_dm_ext;

struct DeviceManager interrupt_dm = {
	.type = DEVICE_TYPE_INTERRUPT_CONTROLLER,

	.ops = &interrupt_dm_ops,

	.private_data = &interrupt_dm_ext,
};

DriverResult interrupt_dm_load(DeviceManager *manager) {
	interrupt_dm_ext.current_device = NULL;

	return DRIVER_OK;
}

/**
 * @brief 检查是否提供了必要的接口
 */
DriverResult check_intterupt_ops(InterruptDeviceOps *int_ops) {
	if (!int_ops->disable_irq) {
		print_error("Interrupt", "no disable_irq operation\n");
		return DRIVER_ERROR_INCOMPLETABLE_OPS;
	}
	if (!int_ops->enable_irq) {
		print_error("Interrupt", "no enable_irq operation\n");
		return DRIVER_ERROR_INCOMPLETABLE_OPS;
	}
	if (!int_ops->eoi) {
		print_error("Interrupt", "no eoi operation\n");
		return DRIVER_ERROR_INCOMPLETABLE_OPS;
	}
	if (!int_ops->redirect_irq) {
		print_error("Interrupt", "no redirect_irq operation\n");
		return DRIVER_ERROR_INCOMPLETABLE_OPS;
	}
	return DRIVER_OK;
}

DriverResult create_interrupt_device(
	InterruptDevice **interrupt_device, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver,
	InterruptDeviceOps *int_ops, int priority) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	DRIVER_RESULT_PASS(check_intterupt_ops(int_ops));

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_INTERRUPT_CONTROLLER);
	if (result != DRIVER_OK) return result;

	*interrupt_device = kmalloc(sizeof(InterruptDevice));
	if (*interrupt_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	InterruptDevice *int_device = *interrupt_device;
	logical_device->dm_ext		= int_device;
	int_device->device			= logical_device;
	int_device->ops				= int_ops;
	int_device->priority		= priority;

	InterruptDevice *current_device = interrupt_dm_ext.current_device;
	if (current_device) {
		if (int_device->priority > current_device->priority) {
			if (current_device->device->state == DEVICE_STATE_ACTIVE) {
				DEV_OPS_CALL(current_device->device, stop);
			}
			interrupt_dm_ext.current_device = int_device;
		}
	} else {
		interrupt_dm_ext.current_device = int_device;
	}

	return DRIVER_OK;
}

DriverResult delete_interrupt_device(InterruptDevice *interrupt_device) {
	InterruptDeviceManager *manager = &interrupt_dm_ext;

	LogicalDevice *cur;
	LogicalDevice *device = interrupt_device->device;

	// 如果是正在使用的设备
	InterruptDevice *new_interrupt_device = NULL;
	if (manager->current_device == interrupt_device) {
		// 寻找替代的设备
		list_for_each_owner (cur, &interrupt_dm.device_lh, dm_list) {
			if (cur != device) {
				if (new_interrupt_device == NULL) {
					new_interrupt_device = cur->dm_ext;
				} else {
					InterruptDevice *cur_interrupt_device = cur->dm_ext;
					if (cur_interrupt_device->priority >
						new_interrupt_device->priority) {
						new_interrupt_device = cur_interrupt_device;
					}
				}
			}
		}
		if (new_interrupt_device == NULL) return DRIVER_ERROR_BUSY;
	}
	if (device->ops->stop && device->state == DEVICE_STATE_ACTIVE)
		DRIVER_RESULT_PASS(device->ops->stop(device));

	LogicalDevice *new_device = new_interrupt_device->device;

	// 恢复运行状态
	if (device->state == DEVICE_STATE_UNINIT) {
		init_and_start_logical_device(new_device);
	} else if (device->state == DEVICE_STATE_READY) {
		start_logical_device(new_device);
	}
	new_device->state		= DEVICE_STATE_ACTIVE;
	manager->current_device = new_interrupt_device;

	list_del(&device->dm_list);
	DRIVER_RESULT_PASS(delete_logical_device(device));
	int result = kfree(interrupt_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;

	return DRIVER_OK;
}

DriverResult interrupt_dm_start() {
	if (interrupt_dm_ext.current_device) {
		LogicalDevice *device = interrupt_dm_ext.current_device->device;
		if (device->physical_device->state != DEVICE_STATE_READY) {
			DRIVER_RESULT_PASS(
				init_and_start_physical_device(device->physical_device));
		}
		init_and_start_logical_device(device);
		return DRIVER_OK;
	}
	return DRIVER_ERROR_NOT_EXIST;
}

uint32_t interrupt_redirect_irq(int irq) {
	InterruptDevice *interrupt_device = interrupt_dm_ext.current_device;

	return interrupt_device->ops->redirect_irq(interrupt_device, irq);
}

DriverResult interrupt_enable_irq(int irq) {
	InterruptDevice *interrupt_device = interrupt_dm_ext.current_device;

	return interrupt_device->ops->enable_irq(interrupt_device, irq);
}

DriverResult interrupt_disable_irq(int irq) {
	InterruptDevice *interrupt_device = interrupt_dm_ext.current_device;

	return interrupt_device->ops->disable_irq(interrupt_device, irq);
}

void interrupt_eoi(int irq) {
	InterruptDevice *interrupt_device = interrupt_dm_ext.current_device;

	interrupt_device->ops->eoi(interrupt_device, irq);
}
