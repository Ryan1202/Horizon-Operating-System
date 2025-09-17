#ifndef _DEVICE_H
#define _DEVICE_H

#include "kernel/driver.h"
#include "kernel/driver_interface.h"
#include "kernel/list.h"
#include "objects/object.h"
#include "objects/permission.h"
#include "stdint.h"
#include "string.h"
#include "types.h"

#define device_print_error(device, str, ...) \
	print_error(device->name.text, str, ##__VA_ARGS__)
// 调用后自动传递错误
#define DEV_OPS_CALL(dm, func, ...)                               \
	{                                                             \
		if ((dm)->ops->func != NULL) {                            \
			DRV_RESULT_DELIVER_CALL((dm)->ops->func, __VA_ARGS__) \
		}                                                         \
	}

typedef enum {
	DEVICE_TYPE_UNKNOWN = 0,
	DEVICE_TYPE_INTERRUPT_CONTROLLER,
	DEVICE_TYPE_TIMER, // 定时器设备
	DEVICE_TYPE_VIDEO,
	DEVICE_TYPE_STORAGE,
	DEVICE_TYPE_MOUSE,
	DEVICE_TYPE_KEYBOARD,
	DEVICE_TYPE_SOUND,
	DEVICE_TYPE_BUS_CONTROLLER,
	DEVICE_TYPE_ETHERNET,
	DEVICE_TYPE_TIME, // 时间设备（如：Unix时间戳/UTC时间）
	DEVICE_TYPE_USB,
	DEVICE_TYPE_MAX,
} DeviceType;

struct Device;

typedef struct DeviceOps {
	DriverResult (*init)(struct Device *dev);	 // 初始化设备
	DriverResult (*start)(struct Device *dev);	 // 启动设备
	DriverResult (*stop)(struct Device *dev);	 // 停止设备
	DriverResult (*destroy)(struct Device *dev); // 销毁设备
	DriverResult (*status)(struct Device *dev);	 // 查询设备状态
} DeviceOps;

typedef enum {
	DEVICE_STATE_UNREGISTERED, // 设备未注册
	DEVICE_STATE_REGISTERED,   // 设备已注册
	DEVICE_STATE_READY,		   // 设备准备就绪
	DEVICE_STATE_ACTIVE,	   // 设备正在运行
	DEVICE_STATE_ERROR,		   // 设备错误
} DeviceState;

typedef struct ChildDevice {
	bool		   is_using;
	uint32_t	   id;
	struct Device *parent;
	void		  *private_data;
} ChildDevice;

struct Bus;
struct Object;
struct DeviceDriver;
typedef struct Device {
	list_t				 bus_list;
	list_t				 device_list;
	list_t				 dm_list;
	string_t			 name;
	DeviceState			 state;
	struct DeviceDriver *device_driver;

	struct Bus *bus;

	struct Object *object;

	DeviceIrq *irq;
	DeviceOps *ops;

	uint32_t	 max_child_device;
	ChildDevice *child_devices;
	void	   **child_private_data;

	void	*private_data;
	uint32_t private_data_size;
	void	*dm_ext; // 设备管理器所需的扩展信息
} Device;

static const Permission device_sys_permission = {
	.subject_id = SUBJECT_ID_SYSTEM,
	.permission = {1, 1, 1, 1, 1, 1, 1},
};
static const Permission device_all_user_permission = {
	.subject_id = SUBJECT_ID_ALL,
	.permission = {1, 1, 0, 1, 0, 0, 0},
};
static const Permission device_owner_permission = {
	.subject_id = SUBJECT_ID_SYSTEM,
	.permission = {1, 1, 1, 1, 1, 1, 1},
};
static const Permission device_admin_permission = {
	.subject_id = SUBJECT_ID_ADMIN,
	.permission = {1, 1, 1, 1, 0, 0, 0},
};

static const ObjectAttr device_object_attr = {
	.type				 = OBJECT_TYPE_DEVICE,
	.size				 = sizeof(Device),
	.is_mounted			 = false,
	.owner_id			 = SUBJECT_ID_SYSTEM,
	.all_user_permission = device_all_user_permission,
	.owner_permission	 = device_owner_permission,
	.system_permission	 = device_sys_permission,
	.admin_permission	 = device_admin_permission,
};

DriverResult register_device(
	struct DeviceDriver *device_driver, string_t name, struct Bus *bus,
	Device *device, ObjectAttr *attr);
DriverResult unregister_device(
	struct DeviceDriver *device_driver, Device *device);
DriverResult unregister_child_device(ChildDevice *child_device);
DriverResult register_child_device(Device *device, int private_data_size);
DriverResult init_device(Device *device);
DriverResult init_and_start(Device *device);

#endif