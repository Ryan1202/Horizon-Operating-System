#ifndef _DRIVER_H
#define _DRIVER_H

#define DRIVER_MAX_NAME_LEN	64
#define DEVICE_MAX_NAME_LEN	64

#include <kernel/list.h>
#include <string.h>

typedef enum {
	FAILED = -1,
	SUCCUESS = 0,
	UNKNOWN
} req_status;

typedef struct
{
	req_status (*driver_enter)(struct _driver_s *drv);
	req_status (*driver_exit)(struct _driver_s *drv);
	req_status (*driver_open)(struct _driver_s *drv);
	req_status (*driver_close)(struct _driver_s *drv);
	req_status (*driver_read)(struct _driver_s *drv, uint32_t *buf, size_t *size);
	req_status (*driver_write)(struct _driver_s *drv, uint32_t *buf, size_t size);
	req_status (*driver_devctl)(struct _driver_s *drv, uint32_t func_num, uint32_t value);
} driver_func_t;

typedef struct _driver_s
{
	list_t list;
	list_t device_list;
	string_t name;
	
	driver_func_t funtion;
} driver_t;

typedef struct
{
	list_t list;
	driver_t *drv_obj;
	void *device_extension;
	string_t name;
} device_t;

req_status driver_create(driver_func_t func, char *driver_name);
req_status device_create(driver_t *driver, unsigned long device_extension_size, char *name, device_t **device);
void device_delete(device_t *device);

#endif