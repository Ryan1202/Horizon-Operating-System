#include <kernel/driver.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <kernel/thread.h>
#include <fs/vfs.h>
#include <const.h>
#include <math.h>

LIST_HEAD(driver_list_head);
struct index_node *dev;

struct file_operations device_fops = {
	.open = dev_open,
	.close = dev_close,
	.read = dev_read,
	.write = dev_write,
	.ioctl = dev_ioctl
};

struct inode *dev_open(char *path)
{
	struct index_node *inode = vfs_open(path);
	if (inode = NULL) return NULL;
	else inode->device->drv_obj->funtion.driver_open(inode->device->drv_obj);
	return inode;
}

int dev_close(struct index_node *inode)
{
	return inode->device->drv_obj->funtion.driver_close(inode->device->drv_obj);
}

int dev_read(struct index_node *inode, char *buffer, uint32_t offset, uint32_t length)
{
	return device_rw(inode->device, 0, buffer, offset, length);
}

int dev_write(struct index_node *inode, char *buffer, uint32_t offset, uint32_t length)
{
	return device_rw(inode->device, 1, buffer, offset, length);
}

int dev_ioctl(struct index_node *inode, uint32_t cmd, uint32_t arg)
{
	return inode->device->drv_obj->funtion.driver_devctl(inode->device, cmd, arg);
}

status_t driver_create(driver_func_t func, char *driver_name)
{
	driver_t *drv_obj;
	int status;
	
	drv_obj = kmalloc(sizeof(driver_t));
	if (drv_obj == NULL)
	{
		return FAILED;
	}
	if (func.driver_enter == NULL)
	{
		return FAILED;
	}
	list_init(&drv_obj->device_list);
	list_init(&drv_obj->list);
	drv_obj->funtion = func;
	status = drv_obj->funtion.driver_enter(drv_obj);
	if (status != SUCCUESS)
	{
		return FAILED;
	}
	string_init(&drv_obj->name);
	string_new(&drv_obj->name, driver_name, DRIVER_MAX_NAME_LEN);
	list_add_tail(&drv_obj->list, &driver_list_head);
	
	return SUCCUESS;
}

status_t device_create(driver_t *driver, unsigned long device_extension_size, char *name, dev_type_t type, device_t **device)
{
	device_t *devobj = kmalloc(sizeof(device_t) + device_extension_size);
	devobj->inode = vfs_create(name, ATTR_DEV, dev);
	list_init(&devobj->inode->childs);
	list_init(&devobj->request_queue_head);
	devobj->inode->device = devobj;
	devobj->inode->f_ops = device_fops;
	devobj->type = type;
	spinlock_init(&devobj->lock);
	if (devobj == NULL)
	{
		return FAILED;
	}
	list_init(&devobj->list);
	if (device_extension_size > 0)
	{
		devobj->device_extension = (void *) (devobj + 1);
	}
	else
	{
		devobj->device_extension = NULL;
	}
	if (string_new(&devobj->name, name, DEVICE_MAX_NAME_LEN))
	{
		kfree(devobj);
		return FAILED;
	}
	
	devobj->drv_obj = driver;
	list_add_tail(&devobj->list, &driver->device_list);
	*device = devobj;
}

void device_delete(device_t *device)
{
	if (device == NULL)
	{
		return;
	}
	driver_t *driver = device->drv_obj;
	if (!list_find(&device->list, &driver->device_list))
	{
		return;
	}
	list_del(&device->list);
	string_del(&driver->name);
	kfree(device);
}

int device_rw(device_t *devobj, int rw, char *buffer, int offset, size_t size)
{
	dev_request_t *queue = kmalloc(sizeof(dev_request_t));
	
	queue->length = size;
	queue->buffer = buffer;
	queue->offset = offset;
	queue->task = get_current_thread();
	
	spin_lock(&devobj->lock);
	if (!list_empty(&devobj->request_queue_head)){
		spin_unlock(&devobj->lock);
		thread_block(TASK_WAITING);
		kfree(queue->buffer);
	}
	else
	{
		list_add_tail(&queue->list, &devobj->request_queue_head);
		spin_unlock(&devobj->lock);
		if (rw == 0)
		{
			devobj->drv_obj->funtion.driver_read(devobj, queue->buffer, queue->offset, queue->length);
		}
		else
		{
			devobj->drv_obj->funtion.driver_write(devobj, queue->buffer, queue->offset, queue->length);
		}
		
		spin_lock(&devobj->lock);
		dev_request_t *next = NULL;
		if (queue->list.next != &devobj->request_queue_head)
		{
			 next = list_next_owner(queue, list);
		}
		list_del(&queue->list);
		kfree(queue);
		spin_unlock(&devobj->lock);
		if (next != NULL)
		{
			thread_unblock(next->task);
		}
	}
	
	return SUCCUESS;
}