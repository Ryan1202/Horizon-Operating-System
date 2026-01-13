#ifndef _OBJECT_ATTR_H
#define _OBJECT_ATTR_H

#include <objects/permission.h>
#include <stdint.h>

typedef enum ObjectType {
	OBJECT_TYPE_TYPE,		 // 表示该对象用于表示一种对象类型
	OBJECT_TYPE_DIRECTORY,	 // 表示该对象是一个目录
	OBJECT_TYPE_DRIVER,		 // 表示该对象是一个驱动程序
	OBJECT_TYPE_DEVICE,		 // 表示该对象是一个设备
	OBJECT_TYPE_FILE,		 // 表示该对象是一个文件
	OBJECT_TYPE_VALUE,		 // 表示该对象是一个值
	OBJECT_TYPE_SYM_LINK,	 // 表示该对象是一个符号链接
	OBJECT_TYPE_PARTITION,	 // 表示该对象是一个分区
	OBJECT_TYPE_VOLUME,		 // 表示该对象是一个卷
	OBJECT_TYPE_BUILTIN_MAX, // 表示对象系统内建类型数量的最大值
} ObjectType;

typedef struct ObjectAttr {
	ObjectType type;
	size_t	   size;

	bool	   is_mounted;
	size_t	   owner_id;
	Permission all_user_permission;
	Permission owner_permission;
	Permission system_permission;
	Permission admin_permission;

	list_t permission_lh;

	struct Object *object;
	void		  *fs_location;
} ObjectAttr;

#endif