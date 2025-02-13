#ifndef _OBJECT_H
#define _OBJECT_H

#include "dyn_array.h"
#include "objects/transfer.h"
#include "stdint.h"
#include "string.h"

typedef enum ObjectResult {
	OBJECT_OK,
	OBJECT_ERROR_MEMORY,
	OBJECT_ERROR_INVALID_OPERATION,
	OBJECT_ERROR_CANNOT_FIND,
	OBJECT_ERROR_ILLEGAL_ARGUMENT,
} ObjectResult;

#define OBJECT_DIR_SIZE_SMALL  8
#define OBJECT_DIR_SIZE_MEDIUM 16
#define OBJECT_DIR_SIZE_LARGE  32

typedef enum ObjectType {
	OBJECT_TYPE_TYPE,		 // 表示该对象用于表示一种对象类型
	OBJECT_TYPE_DIRECTORY,	 // 表示该对象是一个目录
	OBJECT_TYPE_DRIVER,		 // 表示该对象是一个驱动程序
	OBJECT_TYPE_DEVICE,		 // 表示该对象是一个设备
	OBJECT_TYPE_FILE,		 // 表示该对象是一个文件
	OBJECT_TYPE_VALUE,		 // 表示该对象是一个值
	OBJECT_TYPE_SYM_LINK,	 // 表示该对象是一个符号链接
	OBJECT_TYPE_PARTITION,	 // 表示该对象是一个分区
	OBJECT_TYPE_BUILTIN_MAX, // 表示对象系统内建类型数量的最大值
} ObjectType;

struct Partition;
typedef struct Object {
	string_t   name;
	ObjectType type;

	struct Object *parent;
	TransferIn	   in;
	TransferOut	   out;
	union {
		uint32_t type;
		struct {
			DynArray *children;
		} directory;
		struct Driver *driver;
		struct Device *device;
		struct {
		} file;
		struct {
			enum {
				VALUE_TYPE_STRING,
				VALUE_TYPE_INTEGER,
			} type;
			union {
				string_t string;
				size_t	 integer;
			};
		} value;
		struct Object	 *sym_link;
		struct Partition *partition;
	} value;
} Object;

extern Object root_object;
extern Object bus_object;
extern Object driver_object;
extern Object device_object;

ObjectResult init_object_tree();
ObjectResult add_object(Object *parent, Object *child);
ObjectResult init_object_directory(Object *object, size_t block_size);
ObjectResult open_oringinal_object_by_ascii_path(
	char *path, Object **out_object);
// 通过ASCII路径打开对象，对于符号链接会自动解析
ObjectResult open_object_by_ascii_path(char *path, Object **object);
Object		*create_object(Object *parent, string_t name, ObjectType type);
Object		*create_object_directory(Object *parent, string_t name);
void		 show_object_tree();

#define append_object(parent, child) \
	dyn_array_append((parent)->value.directory.children, Object *, (child));

#endif