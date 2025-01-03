#ifndef _DEVICE_H
#define _DEVICE_H

#include "driver/transfer.h"
#include "kernel/driver.h"
#include "kernel/driver_interface.h"
#include "kernel/list.h"
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
	DEVICE_TYPE_TIMER,
	DEVICE_TYPE_VIDEO,
	DEVICE_TYPE_STORAGE,
	DEVICE_TYPE_MOUSE,
	DEVICE_TYPE_KEYBOARD,
	DEVICE_TYPE_SOUND,
	DEVICE_TYPE_BUS_CONTROLLER,
	DEVICE_TYPE_ETHERNET,
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

struct DeviceDriver;

typedef struct ChildDevice {
	bool		   is_using;
	uint32_t	   id;
	struct Device *parent;
	void		  *private_data;
} ChildDevice;

typedef struct Device {
	list_t				 device_list;
	list_t				 dm_list;
	string_t			 name;
	DeviceState			 state;
	struct DeviceDriver *device_driver;

	DeviceIrq *irq;
	Transfer  *transfer;
	DeviceOps *ops;

	uint32_t	 max_child_device;
	ChildDevice *child_devices;
	void	   **child_private_data;

	void	*private_data;
	uint32_t private_data_size;
	void	*device_manager_extension; // 设备管理器所需的扩展信息
} Device;

struct Bus;
DriverResult register_device(
	struct DeviceDriver *device_driver, struct Bus *bus, Device *device);
DriverResult unregister_device(
	struct DeviceDriver *device_driver, Device *device);
DriverResult unregister_child_device(ChildDevice *child_device);
DriverResult register_child_device(Device *device, int private_data_size);
DriverResult init_device(Device *device);
DriverResult init_and_start(Device *device);

#endif