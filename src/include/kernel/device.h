#ifndef _DEVICE_H
#define _DEVICE_H

#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <objects/permission.h>
#include <string.h>
#include <types.h>

#define device_print_error(device, str, ...) \
	print_error(device->name.text, str, ##__VA_ARGS__)
// 调用后自动传递错误
#define DEV_OPS_CALL(dm, func)                      \
	{                                               \
		if ((dm)->ops->func != NULL) {              \
			DRIVER_RESULT_PASS((dm)->ops->func(dm)) \
		}                                           \
	}

typedef enum {
	DEVICE_TYPE_UNKNOWN = 0,
	DEVICE_TYPE_INTERRUPT_CONTROLLER,
	DEVICE_TYPE_TIMER, // 定时器设备
	DEVICE_TYPE_FRAMEBUFFER,
	DEVICE_TYPE_STORAGE,
	DEVICE_TYPE_INPUT,
	DEVICE_TYPE_SOUND,
	DEVICE_TYPE_BUS_CONTROLLER,
	DEVICE_TYPE_ETHERNET,
	DEVICE_TYPE_TIME, // 时间设备（如：Unix时间戳/UTC时间）
	DEVICE_TYPE_SERIAL,
	DEVICE_TYPE_MAX,
} DeviceType;

typedef struct DeviceOps {
	DriverResult (*init)(void *device);	   // 初始化设备
	DriverResult (*start)(void *device);   // 启动设备
	DriverResult (*stop)(void *device);	   // 停止设备
	DriverResult (*destroy)(void *device); // 销毁设备
} DeviceOps;

typedef enum {
	DEVICE_STATE_UNINIT, // 设备未初始化
	DEVICE_STATE_READY,	 // 设备准备就绪
	DEVICE_STATE_ACTIVE, // 设备正在运行
	DEVICE_STATE_ERROR,	 // 设备错误
} DeviceState;

typedef enum DeviceKind {
	DEVICE_KIND_PHYSICAL,
	DEVICE_KIND_LOGICAL,
} DeviceKind;

typedef struct LogicalDevice {
	DeviceKind	kind;
	DeviceState state;
	list_t		new_device_list;

	list_t logical_device_list;
	list_t dm_list;
	list_t device_list;

	DeviceType type;
	DeviceOps *ops;

	struct PhysicalDevice *physical_device;
	struct Object		  *object;
	void				  *dm_ext; // 设备管理器所需的扩展信息

	void *private_data;
} LogicalDevice;

struct Bus;
struct Object;
struct DeviceDriver;
typedef struct PhysicalDevice {
	DeviceKind	kind;
	DeviceState state;
	list_t		new_device_list;

	list_t bus_list;
	list_t logical_device_lh;
	list_t irq_lh;

	int num;

	struct Bus *bus;

	struct Object *object;

	DeviceOps *ops;

	void *private_data;
	void *bus_ext;
} PhysicalDevice;

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
	.size				 = sizeof(PhysicalDevice),
	.is_mounted			 = false,
	.owner_id			 = SUBJECT_ID_SYSTEM,
	.all_user_permission = device_all_user_permission,
	.owner_permission	 = device_owner_permission,
	.system_permission	 = device_sys_permission,
	.admin_permission	 = device_admin_permission,
};

DriverResult create_physical_device(
	PhysicalDevice **physical_device, struct Bus *bus, ObjectAttr *attr);
void register_physical_device(PhysicalDevice *physical_device, DeviceOps *ops);
DriverResult delete_physical_device(PhysicalDevice *physical_device);
DriverResult create_logical_device(
	LogicalDevice **logical_device, PhysicalDevice *physical_device,
	struct DeviceDriver *device_driver, DeviceOps *ops, DeviceType type);
DriverResult delete_logical_device(LogicalDevice *logical_device);
DriverResult init_physical_device(PhysicalDevice *device);
DriverResult start_physical_device(PhysicalDevice *device);
DriverResult init_logical_device(LogicalDevice *device);
DriverResult start_logical_device(LogicalDevice *device);

#define init_and_start_physical_device(device)                           \
	({                                                                   \
		DriverResult result = init_physical_device(device);              \
		if (result == DRIVER_OK) result = start_physical_device(device); \
		result;                                                          \
	})

#define init_and_start_logical_device(device)                           \
	({                                                                  \
		DriverResult result = init_logical_device(device);              \
		if (result == DRIVER_OK) result = start_logical_device(device); \
		result;                                                         \
	})

extern list_t new_physical_device_lh;
extern list_t new_logical_device_lh;

#endif