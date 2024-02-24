/**
 * @file driver.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 驱动接口
 * @version 0.3
 * @date 2022-07-20
 */
#include <const.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <kernel/thread.h>
#include <math.h>

#include <network/eth.h>
#include <network/network.h>

LIST_HEAD(driver_list_head);
list_t irq_handler_lists[16] = {
	LIST_HEAD_INIT(irq_handler_lists[0]),  LIST_HEAD_INIT(irq_handler_lists[1]),
	LIST_HEAD_INIT(irq_handler_lists[2]),  LIST_HEAD_INIT(irq_handler_lists[3]),
	LIST_HEAD_INIT(irq_handler_lists[4]),  LIST_HEAD_INIT(irq_handler_lists[5]),
	LIST_HEAD_INIT(irq_handler_lists[6]),  LIST_HEAD_INIT(irq_handler_lists[7]),
	LIST_HEAD_INIT(irq_handler_lists[8]),  LIST_HEAD_INIT(irq_handler_lists[9]),
	LIST_HEAD_INIT(irq_handler_lists[10]), LIST_HEAD_INIT(irq_handler_lists[11]),
	LIST_HEAD_INIT(irq_handler_lists[12]), LIST_HEAD_INIT(irq_handler_lists[13]),
	LIST_HEAD_INIT(irq_handler_lists[14]), LIST_HEAD_INIT(irq_handler_lists[15]),
};

struct index_node *dev;

struct file_operations device_fops = {
	.open  = dev_open,
	.close = dev_close,
	.read  = dev_read,
	.write = dev_write,
	.ioctl = dev_ioctl,
	.seek  = fs_seek,
};

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
	return inode->device->drv_obj->function.driver_read(inode->device, (uint8_t *)buffer, inode->fp->offset,
														length);
}

int dev_write(struct index_node *inode, uint8_t *buffer, uint32_t length) {
	return inode->device->drv_obj->function.driver_write(inode->device, (uint8_t *)buffer, inode->fp->offset,
														 length);
}

int dev_ioctl(struct index_node *inode, uint32_t cmd, uint32_t arg) {
	return inode->device->drv_obj->function.driver_devctl(inode->device, cmd, arg);
}

void init_dm(void) {
	init_network();
}

status_t driver_create(driver_func_t func, char *driver_name) {
	driver_t *drv_obj;
	int		  status;

	drv_obj = kmalloc(sizeof(driver_t));
	if (drv_obj == NULL) { return FAILED; }
	if (func.driver_enter == NULL) { return FAILED; }
	list_init(&drv_obj->device_list);
	list_init(&drv_obj->list);
	drv_obj->function = func;
	status			  = drv_obj->function.driver_enter(drv_obj);
	if (status == NODEV) {
		printk("[driver manager]Cannot found device:%s\n", driver_name);
	} else if (status != SUCCUESS) {
		return FAILED;
	}
	string_init(&drv_obj->name);
	string_new(&drv_obj->name, driver_name, DRIVER_MAX_NAME_LEN);
	list_add_tail(&drv_obj->list, &driver_list_head);

	return SUCCUESS;
}

void driver_inited() {
	driver_t *cur, *next;
	list_for_each_owner_safe (cur, next, &driver_list_head, list) {
		if (cur->dm != NULL) { cur->dm->drv_inited(cur->dm); }
	}
}

status_t device_create(driver_t *driver, unsigned long device_extension_size, char *name, dev_type_t type,
					   device_t **device) {
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

void device_register_irq(device_t *devobj, int irq, driver_irq_handler_t handler) {
	if (irq > 16) return;
	if (list_empty(&irq_handler_lists[irq])) irq_enable(irq);
	dev_irq_t *dev_irq = kmalloc(sizeof(dev_irq_t));
	dev_irq->devobj	   = devobj;
	dev_irq->handler   = handler;
	list_add_tail(&dev_irq->list, &irq_handler_lists[irq]);
}

void device_unregister_irq(device_t *devobj, int irq) {
	if (irq > 16) return;
	if (list_empty(&irq_handler_lists[irq])) return;
	dev_irq_t *dev_irq, *next;
	list_for_each_owner_safe (dev_irq, next, &irq_handler_lists[irq], list) {
		if (dev_irq->devobj == devobj) {
			list_del(&dev_irq->list);
			kfree(dev_irq);
		}
	}
}

void device_irq_handler(int irq) {
	dev_irq_t *cur, *next;
	if (list_empty(&irq_handler_lists[irq])) return;
	list_for_each_owner_safe (cur, next, &irq_handler_lists[irq], list) {
		cur->handler(cur->devobj, irq);
	}
}
