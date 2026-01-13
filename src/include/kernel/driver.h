#ifndef _DRIVER_H
#define _DRIVER_H

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <objects/attr.h>
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
	DRIVER_OK,
	DRIVER_ERROR_TIMEOUT,
	DRIVER_ERROR_ALREADY_EXIST,
	DRIVER_ERROR_NOT_EXIST,
	DRIVER_ERROR_CONFLICT,
	DRIVER_ERROR_NO_OPS,
	DRIVER_ERROR_INCOMPLETABLE_OPS,
	DRIVER_ERROR_INVALID_IRQ_NUMBER,
	DRIVER_ERROR_OUT_OF_MEMORY,
	DRIVER_ERROR_OBJECT,
	DRIVER_ERROR_NULL_POINTER,
	DRIVER_ERROR_UNSUPPORT_DEVICE,
	DRIVER_ERROR_UNSUPPORT_FEATURE,
	DRIVER_ERROR_BUSY,
	DRIVER_ERROR_WAITING,
	DRIVER_ERROR_EXCEED_MAX_SIZE,
	DRIVER_ERROR_INVALID_TYPE,
	DRIVER_ERROR_MEMORY_FREE,
	DRIVER_ERROR_OTHER,
} DriverResult;

#define DRIVER_RESULT_PASS(func)                    \
	{                                               \
		DriverResult result = func;                 \
		if (result != DRIVER_OK) { return result; } \
	}

typedef enum {
	DRIVER_TYPE_DEVICE_DRIVER = 0,
	DRIVER_TYPE_BUS_DRIVER,
	DRIVER_TYPE_MAX,
} DriverType;

typedef enum {
	DRIVER_STATE_UNREGISTERED, // 驱动未注册
	DRIVER_STATE_REGISTERED,   // 驱动已注册
} DriverState;

// 描述驱动程序的结构，管理着一个驱动下的所有类型的抽象驱动
typedef struct Driver {
	string_t short_name;
	list_t	 device_driver_lh;
	list_t	 remapped_memory_lh;

	DriverState state;
} Driver;

DriverResult register_driver(Driver *driver);
DriverResult unregister_driver(Driver *driver);
DriverResult driver_start_all(void);
void		 add_driver_objects(void);
void		 print_driver_result(
			DriverResult result, char *file, int line, char *func_with_args);

extern list_t		 new_bus_lh;
extern list_t		 bus_check_lh;
extern list_t		 new_device_lh;
extern spinlock_t	 device_list_lock;
extern struct Object driver_object;

#define DRV_PRINT_RESULT(result, func) \
	print_driver_result(result, __FILE__, __LINE__, #func);

#define DRV_RESULT_PRINT_CALL(func)                              \
	{                                                            \
		DriverResult result = func;                              \
		if (result != DRIVER_OK) DRV_PRINT_RESULT(result, func); \
	}

#endif