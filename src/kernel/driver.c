#include <kernel/driver.h>
#include <kernel/memory.h>
#include <kernel/console.h>

LIST_HEAD(driver_list_head);

req_status driver_create(driver_func_t func, char *driver_name)
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

req_status device_create(driver_t *driver, unsigned long device_extension_size, char *name, device_t **device)
{
	device_t *devobj = kmalloc(sizeof(device_t) + device_extension_size);
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

void show_drivers()
{
	driver_t *drvobj, *next;
	list_for_each_owner_safe(drvobj, next, &driver_list_head, list)
	{
		printk("  Driver: %s\n", drvobj->name.text);
		device_t *devobj, *dnext;
		list_for_each_owner_safe(devobj, dnext, &drvobj->device_list, list)
		{
			printk("    Device: %s\n", devobj->name.text);
		}
	}
}