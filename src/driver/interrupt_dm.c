#include <driver/interrupt_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <result.h>
#include <stdint.h>

DriverResult interrupt_start(DeviceManager *manager, Device *device);

DeviceManagerOps interrupt_dm_ops = {
	.dm_load_hook	= NULL,
	.dm_unload_hook = NULL,
};

typedef struct InterruptDeviceManager {
	InterruptDevice *current_device;
} InterruptDeviceManager;

InterruptDeviceManager interrupt_dm_ext;

struct DeviceManager interrupt_device_manager = {
	.type = DEVICE_TYPE_INTERRUPT_CONTROLLER,

	.ops = &interrupt_dm_ops,

	.private_data = &interrupt_dm_ext,
};

/**
 * @brief 检查是否提供了必要的接口
 */
DriverResult check_intterupt_ops(InterruptDevice *interrupt_device) {
	if (!interrupt_device->interrupt_ops) {
		print_error(
			"%s has no operations\n", interrupt_device->device->name.text);
		return DRIVER_RESULT_DEVICE_DRIVER_HAVE_NO_OPS;
	}
	if (!interrupt_device->interrupt_ops->disable_irq) {
		print_error(
			"%s has no disable_irq operation\n",
			interrupt_device->device->name.text);
		return DRIVER_RESULT_DEVICE_DRIVER_HAVE_INCOMPLETABLE_OPS;
	}
	if (!interrupt_device->interrupt_ops->enable_irq) {
		print_error(
			"%s has no enable_irq operation\n",
			interrupt_device->device->name.text);
		return DRIVER_RESULT_DEVICE_DRIVER_HAVE_INCOMPLETABLE_OPS;
	}
	if (!interrupt_device->interrupt_ops->eoi) {
		print_error(
			"%s has no eoi operation\n", interrupt_device->device->name.text);
		return DRIVER_RESULT_DEVICE_DRIVER_HAVE_INCOMPLETABLE_OPS;
	}
	if (!interrupt_device->interrupt_ops->redirect_irq) {
		print_error(
			"%s has no redirect_irq operation\n",
			interrupt_device->device->name.text);
		return DRIVER_RESULT_DEVICE_DRIVER_HAVE_INCOMPLETABLE_OPS;
	}
	return DRIVER_RESULT_OK;
}

DriverResult register_interrupt_device(
	DeviceDriver *device_driver, Device *device,
	InterruptDevice *interrupt_device) {
	interrupt_device->device = device;
	DRV_RESULT_DELIVER_CALL(check_intterupt_ops, interrupt_device);

	DRV_RESULT_DELIVER_CALL(
		register_device, device_driver, device_driver->bus, device);
	list_add_tail(&device->dm_list, &interrupt_device_manager.device_driver_lh);

	InterruptDeviceManager *manager = interrupt_device_manager.private_data;
	if (manager->current_device) {
		if (interrupt_device->priority > manager->current_device->priority) {
			if (manager->current_device->device->state == DEVICE_STATE_ACTIVE) {
				DEV_OPS_CALL(
					manager->current_device->device, stop,
					manager->current_device->device);
			}
			manager->current_device = interrupt_device;
		}
	} else {
		manager->current_device = interrupt_device;
	}

	return DRIVER_RESULT_OK;
}

DriverResult unregister_interrupt_device(
	DeviceDriver *device_driver, Device *device,
	InterruptDevice *interrupt_device) {

	InterruptDeviceManager *manager = interrupt_device_manager.private_data;
	Device				   *cur;

	DEV_OPS_CALL(device, stop, device);
	// 如果是正在使用的设备
	if (manager->current_device == interrupt_device) {
		// 寻找替代的设备
		InterruptDevice *new_interrupt_device;
		list_for_each_owner (
			cur, &interrupt_device_manager.device_driver_lh, device_list) {
			if (cur != device) {
				if (new_interrupt_device == NULL) {
					new_interrupt_device = cur->driver_manager_extension;
				} else {
					InterruptDevice *cur_interrupt_device =
						(InterruptDevice *)cur->driver_manager_extension;
					if (cur_interrupt_device->priority >
						new_interrupt_device->priority) {
						new_interrupt_device = cur_interrupt_device;
					}
				}
			}
		}
		if (new_interrupt_device != NULL) {
			Device		 *new_device		= new_interrupt_device->device;
			DeviceDriver *new_device_driver = new_device->device_driver;
			DRV_RESULT_DELIVER_CALL(
				register_interrupt_device, new_device_driver, new_device,
				new_interrupt_device);
			// 恢复运行状态
			if (device->state == DEVICE_STATE_ACTIVE) {
				DEV_OPS_CALL(new_device, init, new_device);
				DEV_OPS_CALL(new_device, start, new_device);
			} else if (device->state == DEVICE_STATE_READY) {
				DEV_OPS_CALL(new_device, init, new_device);
			}
			manager->current_device = new_interrupt_device;
		}
	}
	DEV_OPS_CALL(device, destroy, device);

	DRV_RESULT_DELIVER_CALL(unregister_device, device_driver, device);
	list_del(&device->dm_list);

	return DRIVER_RESULT_OK;
}

DriverResult interrupt_dm_start() {
	if (interrupt_dm_ext.current_device) {
		Device *device = interrupt_dm_ext.current_device->device;
		DRV_RESULT_DELIVER_CALL(init_device, device);
		DEV_OPS_CALL(device, start, device);
	}
	return DRIVER_RESULT_OK;
}

uint32_t interrupt_redirect_irq(int irq) {
	InterruptDeviceManager *manager = interrupt_device_manager.private_data;
	InterruptDevice		   *interrupt_device = manager->current_device;
	return interrupt_device->interrupt_ops->redirect_irq(interrupt_device, irq);
}

DriverResult interrupt_enable_irq(int irq) {
	InterruptDeviceManager *manager = interrupt_device_manager.private_data;
	InterruptDevice		   *interrupt_device = manager->current_device;
	DRV_RESULT_DELIVER_CALL(
		interrupt_device->interrupt_ops->enable_irq, interrupt_device, irq);
	return DRIVER_RESULT_OK;
}

DriverResult interrupt_disable_irq(int irq) {
	InterruptDeviceManager *manager = interrupt_device_manager.private_data;
	InterruptDevice		   *interrupt_device = manager->current_device;
	DRV_RESULT_DELIVER_CALL(
		interrupt_device->interrupt_ops->disable_irq, interrupt_device, irq);
	return DRIVER_RESULT_OK;
}

void interrupt_eoi(int irq) {
	InterruptDeviceManager *manager = interrupt_device_manager.private_data;
	InterruptDevice		   *interrupt_device = manager->current_device;
	interrupt_device->interrupt_ops->eoi(interrupt_device, irq);
}
