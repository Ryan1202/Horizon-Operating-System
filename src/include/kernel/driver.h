#ifndef _DRIVER_H
#define _DRIVER_H

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <objects/object.h>
#include <objects/permission.h>
#include <result.h>
#include <string.h>

static const Permission driver_sys_permission = {
	.subject_id = SUBJECT_ID_SYSTEM,
	.permission = {1, 1, 1, 1, 1, 1, 1},
};
static const Permission driver_all_user_permission = {
	.subject_id = SUBJECT_ID_ALL,
	.permission = {1, 1, 0, 1, 0, 0, 0},
};
static const Permission driver_owner_permission = {
	.subject_id = SUBJECT_ID_SYSTEM,
	.permission = {1, 1, 1, 1, 1, 1, 1},
};
static const Permission driver_admin_permission = {
	.subject_id = SUBJECT_ID_ADMIN,
	.permission = {1, 1, 1, 1, 0, 0, 0},
};
static const ObjectAttr driver_object_attr = {
	.type				 = OBJECT_TYPE_DRIVER,
	.size				 = 0,
	.owner_permission	 = driver_owner_permission,
	.system_permission	 = driver_sys_permission,
	.all_user_permission = driver_all_user_permission,
	.admin_permission	 = driver_admin_permission,
};

typedef enum DriverResult {
	DRIVER_RESULT_OK,
	DRIVER_RESULT_TIMEOUT,
	DRIVER_RESULT_ALREADY_EXIST,
	DRIVER_RESULT_NOT_EXIST,
	DRIVER_RESULT_DEVICE_DRIVER_CONFLICT,
	DRIVER_RESULT_NO_OPS,
	DRIVER_RESULT_INCOMPLETABLE_OPS,
	DRIVER_RESULT_NO_VALID_CHILD_DEVICE,
	DRIVER_RESULT_INVALID_IRQ_NUMBER,
	DRIVER_RESULT_OUT_OF_MEMORY,
	DRIVER_RESULT_NULL_POINTER,
	DRIVER_RESULT_UNSUPPORT_DEVICE,
	DRIVER_RESULT_UNSUPPORT_FEATURE,
	DRIVER_RESULT_BUSY,
	DRIVER_RESULT_EXCEED_MAX_SIZE,
	DRIVER_RESULT_INVALID_TYPE,
	DRIVER_RESULT_OTHER_ERROR,
} DriverResult;

#define DRIVER_RESULT_PASS(func)                           \
	{                                                      \
		DriverResult result = func;                        \
		if (result != DRIVER_RESULT_OK) { return result; } \
	}

typedef enum {
	DRIVER_TYPE_DEVICE_DRIVER = 0,
	DRIVER_TYPE_BUS_DRIVER,
	DRIVER_TYPE_MAX,
} DriverType;

typedef enum {
	DRIVER_STATE_UNREGISTERED, // 驱动未注册
	DRIVER_STATE_UNINITED,	   // 驱动未初始化
	DRIVER_STATE_ACTIVE,	   // 驱动正在运行
} DriverState;

typedef enum {
	SUBDRIVER_STATE_UNREGISTERED, // 子驱动未注册
	SUBDRIVER_STATE_UNREADY,	  // 子驱动未准备好
	SUBDRIVER_STATE_READY,		  // 子驱动准备好
} SubDriverState;

// 描述驱动程序的结构，管理着一个驱动下的所有类型的抽象驱动
typedef struct Driver {
	string_t short_name;
	list_t	 driver_list;
	list_t	 sub_driver_lh;
	list_t	 remapped_memory_lh;

	DriverState state;

	int						 dependency_count;
	struct DriverDependency *dependencies;

	DriverResult (*init)(struct Driver *driver);
} Driver;

struct DriverDenpendency;
typedef struct SubDriver {
	list_t		   list;
	list_t		   sub_driver_list;
	Driver		  *driver;
	DriverType	   type;
	SubDriverState state;

	WaitQueue wq;
} SubDriver;

extern list_t startup_dm_lh;

DriverResult register_driver(Driver *driver);
DriverResult unregister_driver(Driver *driver);
DriverResult register_sub_driver(
	Driver *driver, SubDriver *sub_driver, DriverType type);
DriverResult unregister_sub_driver(Driver *driver, SubDriver *sub_driver);
DriverResult driver_init(Driver *driver);
DriverResult driver_start_all(void);
void		 add_driver_objects(void);
void		 print_driver_result(
			DriverResult result, char *file, int line, char *func_with_args);

extern struct Object driver_object;

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