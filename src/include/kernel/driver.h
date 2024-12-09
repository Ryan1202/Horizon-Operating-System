#ifndef _DRIVER_H
#define _DRIVER_H

#include "stdint.h"
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
	status_t (*driver_read)(
		struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
	status_t (*driver_write)(
		struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
	status_t (*driver_devctl)(
		struct _device_s *dev, uint32_t func_num, uint32_t value);
} driver_func_t;

#define DEV_READ(device, buf, offset, size) \
	device->drv_obj->function.driver_read(  \
		device, (uint8_t *)buf, (uint32_t)offset, (size_t)size)
#define DEV_WRITE(device, buf, offset, size) \
	device->drv_obj->function.driver_write(  \
		device, (uint8_t *)buf, (uint32_t)offset, (size_t)size)
#define DEV_CTL(device, func, value)         \
	device->drv_obj->function.driver_devctl( \
		device, (uint32_t)func, (uint32_t)value)

typedef struct _device_manager_s {
	string_t name;
	list_t	 dev_listhead;
	void	*private_data;

	void (*dm_register)(
		struct _device_manager_s *dm, struct _device_s *dev, char *name);
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

typedef void (*driver_irq_handler_t)(device_t *devobj, int irq);
typedef struct {
	list_t				 list;
	device_t			*devobj;
	driver_irq_handler_t handler;
} dev_irq_t;

extern struct index_node *dev;

void			   init_dm(void);
void			   dm_start(void);
struct index_node *dev_open(char *path);
int				   dev_close(struct index_node *inode);
int		 dev_read(struct index_node *inode, uint8_t *buffer, uint32_t length);
int		 dev_write(struct index_node *inode, uint8_t *buffer, uint32_t length);
int		 dev_ioctl(struct index_node *inode, uint32_t cmd, uint32_t arg);
status_t driver_create(driver_func_t func, char *driver_name);
status_t device_create(
	driver_t *driver, unsigned long device_extension_size, char *name,
	dev_type_t type, device_t **device);
void device_delete(device_t *device);
void device_register_irq(
	device_t *devobj, int irq, driver_irq_handler_t handler);
void device_unregister_irq(device_t *devobj, int irq);
void driver_inited();

// --------new--------

#include "result.h"

typedef enum {
	DRIVER_TYPE_DEVICE_DRIVER = 0,
	DRIVER_TYPE_BUS_DRIVER,
	DRIVER_TYPE_MAX,
} DriverType;

extern list_t startup_dm_lh;

// 实体的驱动，管理着一个驱动下的所有类型的抽象驱动
typedef struct Driver {
	string_t name;
	list_t	 driver_list;
	list_t	 sub_driver_lh;
	list_t	 remapped_memory_lh;

	int						  dependency_count;
	struct DriverDenpendency *dependencies;
} Driver;

struct DriverDenpendency;
typedef struct SubDriver {
	list_t	   list;
	list_t	   sub_driver_list;
	Driver	  *driver;
	DriverType type;
} SubDriver;

typedef enum DriverResult {
	DRIVER_RESULT_OK,
	DRIVER_RESULT_TIMEOUT,
	DRIVER_RESULT_BUS_DRIVER_ALREADY_EXIST,
	DRIVER_RESULT_BUS_DRIVER_NOT_EXIST,
	DRIVER_RESULT_DRIVER_MANAGER_NOT_EXIST,
	DRIVER_RESULT_DEVICE_MANAGER_NOT_EXIST,
	DRIVER_RESULT_DEVICE_NOT_EXIST,
	DRIVER_RESULT_DEVICE_DRIVER_HAVE_NO_OPS,
	DRIVER_RESULT_DEVICE_DRIVER_HAVE_INCOMPLETABLE_OPS,
	DRIVER_RESULT_INVALID_IRQ_NUMBER,
	DRIVER_RESULT_OUT_OF_MEMORY,
	DRIVER_RESULT_NULL_POINTER,
	DRIVER_RESULT_UNSUPPORT_DEVICE,
	DRIVER_RESULT_OTHER_ERROR,
} DriverResult;

DriverResult register_driver(Driver *driver);
DriverResult unregister_driver(Driver *driver);
DriverResult register_sub_driver(Driver *driver, SubDriver *sub_driver);
DriverResult unregister_sub_driver(Driver *driver, SubDriver *sub_driver);
void		 print_driver_result(
			DriverResult result, char *file, int line, char *func_with_args);

#define DRV_PRINT_RESULT(result, func, ...) \
	print_driver_result(result, __FILE__, __LINE__, #func "(" #__VA_ARGS__ ")");
#define DRV_RESULT_DELIVER_CALL(func, ...)    \
	RESULT_DELIVER_CALL(                      \
		DriverResult, DRIVER_RESULT_OK, func, \
		{ DRV_PRINT_RESULT(result, func, ...); }, __VA_ARGS__)

#define DRV_RESULT_PRINT_CALL(func, ...)             \
	{                                                \
		DriverResult result = func(__VA_ARGS__);     \
		DRV_PRINT_RESULT(result, func, __VA_ARGS__); \
	}

#endif