/**
 * @file driver.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 驱动接口
 * @version 0.3
 * @date 2022-07-20
 */
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/thread.h>

#include <network/eth.h>
#include <network/network.h>

LIST_HEAD(driver_list_head);
// list_t device_irq_lists[16] = {
// 	LIST_HEAD_INIT(device_irq_lists[0]),  LIST_HEAD_INIT(device_irq_lists[1]),
// 	LIST_HEAD_INIT(device_irq_lists[2]),  LIST_HEAD_INIT(device_irq_lists[3]),
// 	LIST_HEAD_INIT(device_irq_lists[4]),  LIST_HEAD_INIT(device_irq_lists[5]),
// 	LIST_HEAD_INIT(device_irq_lists[6]),  LIST_HEAD_INIT(device_irq_lists[7]),
// 	LIST_HEAD_INIT(device_irq_lists[8]),  LIST_HEAD_INIT(device_irq_lists[9]),
// 	LIST_HEAD_INIT(device_irq_lists[10]), LIST_HEAD_INIT(device_irq_lists[11]),
// 	LIST_HEAD_INIT(device_irq_lists[12]), LIST_HEAD_INIT(device_irq_lists[13]),
// 	LIST_HEAD_INIT(device_irq_lists[14]), LIST_HEAD_INIT(device_irq_lists[15]),
// };

struct index_node *dev;

struct file_operations device_fops = {
	.open  = dev_open,
	.close = dev_close,
	.read  = dev_read,
	.write = dev_write,
	.ioctl = dev_ioctl,
	.seek  = fs_seek,
};

// --------new--------
#include <kernel/device_driver.h>
#include <kernel/driver_manager.h>
#include <result.h>

LIST_HEAD(driver_lh);

void print_driver_result(
	DriverResult result, char *file, int line, char *func_with_args) {
	if (result == DRIVER_RESULT_OK) return;
	printk("[At file %s line%d: %s]", file, line, func_with_args);
	switch (result) {
		RESULT_CASE_PRINT(DRIVER_RESULT_OK)
		RESULT_CASE_PRINT(DRIVER_RESULT_TIMEOUT)
		RESULT_CASE_PRINT(DRIVER_RESULT_DEVICE_DRIVER_CONFLICT)
		RESULT_CASE_PRINT(DRIVER_RESULT_DEVICE_DRIVER_HAVE_NO_OPS)
		RESULT_CASE_PRINT(DRIVER_RESULT_DEVICE_DRIVER_HAVE_INCOMPLETABLE_OPS)
		RESULT_CASE_PRINT(DRIVER_RESULT_INVALID_IRQ_NUMBER)
		RESULT_CASE_PRINT(DRIVER_RESULT_OUT_OF_MEMORY)
		RESULT_CASE_PRINT(DRIVER_RESULT_BUS_DRIVER_ALREADY_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_BUS_DRIVER_NOT_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_DEVICE_MANAGER_NOT_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_DEVICE_NOT_EXIST)
		RESULT_CASE_PRINT(DRIVER_RESULT_NULL_POINTER)
		RESULT_CASE_PRINT(DRIVER_RESULT_UNSUPPORT_DEVICE)
		RESULT_CASE_PRINT(DRIVER_RESULT_OTHER_ERROR)
	}
}

DriverResult register_driver(Driver *driver) {

	driver->state = DRIVER_STATE_UNINITED;
	list_init(&driver->sub_driver_lh);
	list_init(&driver->remapped_memory_lh);
	list_add_tail(&driver->driver_list, &driver_lh);
	return DRIVER_RESULT_OK;
}

DriverResult unregister_driver(Driver *driver) {
	list_del(&driver->driver_list);
	return DRIVER_RESULT_OK;
}

DriverResult register_sub_driver(
	Driver *driver, SubDriver *sub_driver, DriverType type) {
	sub_driver->driver = driver;
	sub_driver->state  = SUBDRIVER_STATE_UNREADY;
	sub_driver->type   = type;

	list_add(&sub_driver->sub_driver_list, &driver->sub_driver_lh);

	return DRIVER_RESULT_OK;
}

DriverResult unregister_sub_driver(Driver *driver, SubDriver *sub_driver) {
	list_del(&sub_driver->sub_driver_list);

	return DRIVER_RESULT_OK;
}

DriverResult driver_init(Driver *driver) {
	DriverResult result;
	if (driver->init != NULL) {
		result = driver->init(driver);
		if (result != DRIVER_RESULT_OK) {
			driver->state = DRIVER_STATE_UNREGISTERED;
			unregister_driver(driver);
			print_error(
				"driver_init: driver %s init failed!\n", driver->name.text);
			return result;
		}
	}
	driver->state = DRIVER_STATE_ACTIVE;
	return DRIVER_RESULT_OK;
}

void sub_driver_start_thread(void *arg) {
	SubDriver *sub_driver = arg;
	if (sub_driver->type == DRIVER_TYPE_DEVICE_DRIVER) {
		DeviceDriver *device_driver =
			container_of(sub_driver, DeviceDriver, subdriver);
		Device *device;
		list_for_each_owner (device, &device_driver->device_lh, device_list) {
			if (device->ops->init != NULL) { device->ops->init(device); }
			if (device->ops->start != NULL) { device->ops->start(device); }
		}
		device_driver->subdriver.state = SUBDRIVER_STATE_READY;
	} else if (sub_driver->type == DRIVER_TYPE_BUS_DRIVER) {
		BusDriver *bus_driver = container_of(sub_driver, BusDriver, subdriver);
		if (bus_driver->ops->init != NULL) {
			bus_driver->ops->init(bus_driver);
		}

		Bus *bus;
		list_for_each_owner (bus, &bus_driver->bus_lh, bus_list) {
			// 先等待Bus Controller Device就绪
			while (bus->controller_device->device_driver->subdriver.state !=
				   SUBDRIVER_STATE_READY) {
				schedule();
			}
			if (bus->ops->scan_bus != NULL) {
				bus->ops->scan_bus(bus_driver, bus);
			}
		}
	}
}

void driver_start_thread(void *arg) {
	Driver *driver = arg;
	check_dependency(driver);
	driver_init(driver);

	SubDriver *sub_driver;
	list_for_each_owner (sub_driver, &driver->sub_driver_lh, sub_driver_list) {
		thread_start(
			"sub_driver_start_thread", THREAD_DEFAULT_PRIO,
			sub_driver_start_thread, sub_driver);
	}
}

DriverResult driver_start_all(void) {
	Driver *driver;
	list_for_each_owner (driver, &driver_lh, driver_list) {
		if (driver->state == DRIVER_STATE_UNINITED) {
			thread_start(
				"driver_start_thread", THREAD_DEFAULT_PRIO, driver_start_thread,
				driver);
		}
	}
	return DRIVER_RESULT_OK;
}

// --------old---------
struct index_node *dev_open(char *path) {
	struct index_node *inode = vfs_open(path);
	if (inode == NULL) return NULL;
	else inode->device->drv_obj->function.driver_open(inode->device);
	return inode;
}

int dev_close(struct index_node *inode) {
	return inode->device->drv_obj->function.driver_close(inode->device);
}

int dev_read(struct index_node *inode, uint8_t *buffer, uint32_t length) {
	return inode->device->drv_obj->function.driver_read(
		inode->device, (uint8_t *)buffer, inode->fp->offset, length);
}

int dev_write(struct index_node *inode, uint8_t *buffer, uint32_t length) {
	return inode->device->drv_obj->function.driver_write(
		inode->device, (uint8_t *)buffer, inode->fp->offset, length);
}

int dev_ioctl(struct index_node *inode, uint32_t cmd, uint32_t arg) {
	return inode->device->drv_obj->function.driver_devctl(
		inode->device, cmd, arg);
}

void init_dm(void) {
	init_network();
}

status_t driver_create(driver_func_t func, char *driver_name) {
	driver_t *drv_obj;
	int		  status;

	// drv_obj = kmalloc(sizeof(driver_t));
	// if (drv_obj == NULL) { return FAILED; }
	// if (func.driver_enter == NULL) { return FAILED; }
	// list_init(&drv_obj->device_list);
	// list_init(&drv_obj->list);
	// drv_obj->function = func;
	// status			  = drv_obj->function.driver_enter(drv_obj);
	// if (status == NODEV) {
	// 	printk("[driver manager]Cannot found device:%s\n", driver_name);
	// } else if (status != SUCCUESS) {
	// 	return FAILED;
	// }
	// string_init(&drv_obj->name);
	// string_new(&drv_obj->name, driver_name, DRIVER_MAX_NAME_LEN);
	// list_add_tail(&drv_obj->list, &driver_list_head);

	return SUCCUESS;
}

void driver_inited() {
	driver_t *cur;
	list_for_each_owner (cur, &driver_list_head, list) {
		if (cur->dm != NULL) { cur->dm->drv_inited(cur->dm); }
	}
}

status_t device_create(
	driver_t *driver, unsigned long device_extension_size, char *name,
	dev_type_t type, device_t **device) {
	device_t *devobj = kmalloc(sizeof(device_t) + device_extension_size);
	devobj->type	 = type;
	spinlock_init(&devobj->lock);
	if (devobj == NULL) { return FAILED; }
	list_init(&devobj->list);
	if (device_extension_size > 0) {
		devobj->device_extension = (void *)(devobj + 1);
	} else {
		devobj->device_extension = NULL;
	}
	if (string_new(&devobj->name, name, DEVICE_MAX_NAME_LEN)) {
		kfree(devobj);
		return FAILED;
	}

	devobj->drv_obj = driver;
	list_add_tail(&devobj->list, &driver->device_list);
	*device = devobj;

	if (type == DEV_ETH_NET) {
		eth_dm.dm_register(&eth_dm, devobj, name);
		driver->dm = &eth_dm;
	} else {
		devobj->inode		  = vfs_create(name, ATTR_DEV, dev);
		devobj->inode->device = devobj;
		devobj->inode->f_ops  = device_fops;
		devobj->inode->fp	  = kmalloc(sizeof(struct file));
	}

	return SUCCUESS;
}

void device_delete(device_t *device) {
	if (device == NULL) { return; }
	driver_t *driver = device->drv_obj;
	if (!list_find(&device->list, &driver->device_list)) { return; }
	list_del(&device->list);
	string_del(&driver->name);
	kfree(device);
}

void device_register_irq(
	device_t *devobj, int irq, driver_irq_handler_t handler) {
	if (irq > 16) return;
	// if (list_empty(&device_irq_lists[irq])) irq_enable(irq);
	dev_irq_t *dev_irq = kmalloc(sizeof(dev_irq_t));
	dev_irq->devobj	   = devobj;
	dev_irq->handler   = handler;
	// list_add_tail(&dev_irq->list, &device_irq_lists[irq]);
}

void device_unregister_irq(device_t *devobj, int irq) {
	if (irq > 16) return;
	// if (list_empty(&device_irq_lists[irq])) return;
	dev_irq_t *dev_irq, *next;
	// list_for_each_owner_safe (dev_irq, next, &device_irq_lists[irq], list) {
	if (dev_irq->devobj == devobj) {
		list_del(&dev_irq->list);
		kfree(dev_irq);
	}
	// }
}
