#ifndef _OBJECT_H
#define _OBJECT_H

#include <dyn_array.h>
#include <kernel/device.h>
#include <kernel/list.h>
#include <objects/attr.h>
#include <objects/transfer.h>
#include <stdint.h>
#include <string.h>

typedef enum ObjectResult {
	OBJECT_OK,
	OBJECT_ERROR_MEMORY,
	OBJECT_ERROR_DELETE_DIRECTORY,
	OBJECT_ERROR_INVALID_OPERATION,
	OBJECT_ERROR_CANNOT_FIND,
	OBJECT_ERROR_ILLEGAL_ARGUMENT,
	OBJECT_ERROR_OCCUPIED,
	OBJECT_ERROR_ALREADY_EXISTS,
	OBJECT_ERROR_NOT_EMPTY,
	OBJECT_ERROR_NO_PERMISSION,
	OBJECT_ERROR_OTHER,
} ObjectResult;

#define OBJ_RESULT_PASS(call)                       \
	{                                               \
		ObjectResult result = call;                 \
		if (result != OBJECT_OK) { return result; } \
	}

#define OBJECT_DIR_SIZE_SMALL  8
#define OBJECT_DIR_SIZE_MEDIUM 16
#define OBJECT_DIR_SIZE_LARGE  32

struct Partition;
typedef struct Object {
	list_t list;

	string_t name;

	struct Object *parent;
	TransferIn	   in;
	TransferOut	   out;

	uint32_t reference;

	ObjectAttr			  *attr;
	struct Object		  *origin;
	struct FileSystemInfo *fs_info;

	union {
		uint32_t type;
		struct {
			void  *data;
			list_t children;
			void  *fs_iterator;
		} directory;
		struct Driver *driver;
		struct {
			DeviceKind kind;
			union {
				struct PhysicalDevice *physical;
				struct LogicalDevice  *logical;
			};
		} device;
		struct {
			void  *data;
			size_t size;
			size_t offset;
			void  *buffer;
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

	void (*release_data)(struct Object *object);
} Object;

typedef struct ObjectIterator {
	Object *parent_object;
	list_t *current_node;
	void   *fs_iterator;
	enum {
		ITERATOR_TYPE_MEM,
		ITERATOR_TYPE_FS,
	} type;
} ObjectIterator;

static const Permission base_obj_sys_perm = {
	.subject_id = SUBJECT_ID_SYSTEM,
	.permission = {1, 1, 1, 1, 0, 1, 1},
};
static const Permission base_obj_all_user_perm = {
	.subject_id = SUBJECT_ID_ALL,
	.permission = {1, 0, 0, 1, 0, 0, 0},
};
static const Permission base_obj_admin_perm = {
	.subject_id = SUBJECT_ID_ADMIN,
	.permission = {1, 1, 0, 1, 0, 0, 0},
};

static const ObjectAttr base_obj_sys_attr = {
	.type				 = OBJECT_TYPE_DIRECTORY,
	.size				 = 0,
	.owner_id			 = SUBJECT_ID_SYSTEM,
	.owner_permission	 = base_obj_sys_perm,
	.system_permission	 = base_obj_sys_perm,
	.all_user_permission = base_obj_all_user_perm,
	.admin_permission	 = base_obj_admin_perm,
};

extern Object root_object;
extern Object bus_object;
extern Object driver_object;
extern Object device_object;
extern Object volumes_object;

void		 init_object_directory(Object *object);
void		 init_base_obj_sys_attr(Object *object);
ObjectResult init_object_tree();
ObjectResult add_object(Object *parent, Object *child);
// 通过路径打开对象，对于符号链接会自动解析
ObjectResult open_oringinal_object_by_path(char *path, Object **out_object);
ObjectResult open_object_by_path(char *path, Object **object);
Object		*create_object(Object *parent, string_t *name, ObjectAttr attr);
ObjectResult delete_object(Object *object);
Object		*create_object_directory(
		 Object *parent, string_t *name, ObjectAttr attr);
void object_close(Object *object);
void show_object_tree();

#define append_object(parent, child) \
	dyn_array_append((parent)->value.directory.children, Object *, (child));

#endif