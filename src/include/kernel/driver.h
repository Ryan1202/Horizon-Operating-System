#ifndef _DRIVER_H
#define _DRIVER_H

#define DRIVER_MAX_NAME_LEN 64
#define DEVICE_MAX_NAME_LEN 64

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <string.h>

typedef enum {
	UNSUPPORT = -3,
	NODEV	  = -2,
	FAILED	  = -1,
	SUCCUESS  = 0,
	UNKNOWN,
} status_t;

typedef enum {
	DEV_UNKNOWN = 0,
	DEV_STORAGE,
	DEV_MANAGER,
	DEV_KEYBOARD,
	DEV_MOUSE,
	DEV_USB,
	DEV_SOUND,
	DEV_ETH_NET,
} dev_type_t;

typedef struct _device_s {
	list_t	   list;
	spinlock_t lock;
	dev_type_t type;

	struct index_node *inode;
	struct _driver_s  *drv_obj;
	void			  *device_extension;
	string_t		   name;
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

#define DEV_READ(device, buf, offset, size) \
	device->drv_obj->function.driver_read(device, (uint8_t *)buf, (uint32_t)offset, (size_t)size)
#define DEV_WRITE(device, buf, offset, size) \
	device->drv_obj->function.driver_write(device, (uint8_t *)buf, (uint32_t)offset, (size_t)size)
#define DEV_CTL(device, func, value) \
	device->drv_obj->function.driver_devctl(device, (uint32_t)func, (uint32_t)value)

typedef struct _device_manager_s {
	string_t name;
	list_t	 dev_listhead;
	void	*private_data;

	void (*dm_register)(struct _device_manager_s *dm, struct _device_s *dev, char *name);
	void (*dm_unregister)(struct _device_manager_s *dm, struct _device_s *dev);

	void (*drv_inited)(struct _device_manager_s *dm);
} device_manager_t;

typedef struct _driver_s {
	list_t	 list;
	list_t	 device_list;
	string_t name;

	driver_func_t function;

	struct _device_manager_s *dm;
} driver_t;

extern struct index_node *dev;

void			   init_dm(void);
void			   dm_start(void);
struct index_node *dev_open(char *path);
int				   dev_close(struct index_node *inode);
int				   dev_read(struct index_node *inode, uint8_t *buffer, uint32_t length);
int				   dev_write(struct index_node *inode, uint8_t *buffer, uint32_t length);
int				   dev_ioctl(struct index_node *inode, uint32_t cmd, uint32_t arg);
status_t		   driver_create(driver_func_t func, char *driver_name);
status_t device_create(driver_t *driver, unsigned long device_extension_size, char *name, dev_type_t type,
					   device_t **device);
void	 device_delete(device_t *device);
void	 driver_inited();

#endif