#ifndef _DRIVER_H
#define _DRIVER_H

#define DRIVER_MAX_NAME_LEN 64
#define DEVICE_MAX_NAME_LEN 64

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <string.h>

typedef enum { UNSUPPORT = -3, NODEV = -2, FAILED = -1, SUCCUESS = 0, UNKNOWN } status_t;

typedef enum { DEV_UNKNOWN = 0, DEV_STORAGE, DEV_MANAGER, DEV_KEYBOARD, DEV_MOUSE, DEV_USB } dev_type_t;

typedef struct _device_s {
	list_t	   list;
	list_t	   request_queue_head;
	spinlock_t lock;
	dev_type_t type;

	struct index_node *inode;
	struct _driver_s  *drv_obj;
	void			  *device_extension;
	string_t		   name;

	list_t listm; // 特定驱动管理程序用
} device_t;

typedef struct {
	status_t (*driver_enter)(struct _driver_s *drv);
	status_t (*driver_exit)(struct _driver_s *drv);
	status_t (*driver_open)(struct _device_s *dev);
	status_t (*driver_close)(struct _device_s *dev);
	status_t (*driver_read)(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
	status_t (*driver_write)(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
	status_t (*driver_devctl)(struct _device_s *dev, uint32_t func_num, uint32_t value);
} driver_func_t;

typedef struct _driver_s {
	list_t	 list;
	list_t	 device_list;
	string_t name;

	driver_func_t funtion;
} driver_t;

extern struct index_node *dev;

struct index_node *dev_open(char *path);
int				   dev_close(struct index_node *inode);
int				   dev_read(struct index_node *inode, uint8_t *buffer, uint32_t length);
int				   dev_write(struct index_node *inode, uint8_t *buffer, uint32_t length);
int				   dev_ioctl(struct index_node *inode, uint32_t cmd, uint32_t arg);
status_t		   driver_create(driver_func_t func, char *driver_name);
status_t device_create(driver_t *driver, unsigned long device_extension_size, char *name, dev_type_t type,
					   device_t **device);
void	 device_delete(device_t *device);
int		 device_rw(device_t *devobj, int rw, char *buffer, int offset, size_t size);

#endif