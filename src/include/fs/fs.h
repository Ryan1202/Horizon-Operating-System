#ifndef _FS_H
#define _FS_H

#include "objects/handle.h"
#include "string.h"
#include <fs/vfs.h>
#include <kernel/list.h>
#include <objects/object.h>
#include <result.h>

typedef enum FsResult {
	FS_OK,
	FS_ERROR_UNSUPPORT,
	FS_ERROR_INVALID_PATH_OR_NAME,
	FS_ERROR_OUT_OF_MEMORY,
	FS_ERROR_NO_SPARE_SPACE,
	FS_ERROR_CANNOT_FIND,
	FS_ERROR_ALREADY_EXISTS,
	FS_ERROR_NOT_EMPTY,
	FS_ERROR_END_OF_FILE,
	FS_ERROR_ILLEGAL_DATA,
	FS_ERROR_INVALID_PARAMS,
	FS_ERROR_NOT_MATCH,
	FS_ERROR_TRANSFER,
	FS_ERROR_OTHER,
} FsResult;

#define FS_RESULT_PASS(call)                    \
	{                                           \
		FsResult result = call;                 \
		if (result != FS_OK) { return result; } \
	}

struct Partition;
struct FileSystemInfo;
typedef struct FileSystemOps {
	FsResult (*fs_check)(struct Partition *partition);
	FsResult (*fs_mount)(
		struct FileSystemInfo *fs_info, struct Object *root_object);
} FileSystemOps;

struct Volume;
typedef struct FsFileOps {
	FsResult (*fs_open)(
		struct FileSystemInfo *fs_info, Object *parent_obj, ObjectAttr *attr,
		string_t *name, Object **object);
	FsResult (*fs_close)(struct Object *object);
	FsResult (*fs_seek)(struct Object *object, size_t offset);
	FsResult (*fs_read)(
		struct Object *object, struct ObjectHandle *handle, void *buf,
		size_t size);
	FsResult (*fs_write)(
		struct Object *object, struct ObjectHandle *handle, void *buf,
		size_t size);
	FsResult (*fs_get_attr)(struct Object *object, struct ObjectAttr *attr);
	FsResult (*fs_set_attr)(struct Object *object, struct ObjectAttr *attr);

	FsResult (*fs_create_handle)(struct ObjectHandle *handle);
	FsResult (*fs_delete_handle)(struct ObjectHandle *handle);
} FsFileOps;

typedef struct FsDirectoryOps {
	FsResult (*fs_lookup)(
		struct FileSystemInfo *fs_info, Object *parent_obj, string_t *name,
		ObjectAttr **attr);
	FsResult (*fs_opendir)(
		struct FileSystemInfo *fs_info, Object *parent_obj, void **iterator);
	FsResult (*fs_readdir)(
		struct FileSystemInfo *fs_info, ObjectIterator *iterator,
		Object **object);
	FsResult (*fs_closedir)(ObjectIterator *iterator);

	FsResult (*fs_create_file)(
		struct Object *parent_obj, string_t *name, struct Object **object);
	FsResult (*fs_delete_file)(
		struct Object *parent_obj, ObjectAttr *attr, string_t *name);

	FsResult (*fs_mkdir)(
		struct Object *parent_obj, string_t *name, struct Object **object);
	FsResult (*fs_rmdir)(
		struct Object *parent_obj, ObjectAttr *attr, string_t *name);

	FsResult (*fs_get_attr)(struct Object *object, struct ObjectAttr *attr);
	FsResult (*fs_set_attr)(struct Object *object, struct ObjectAttr *attr);
} FsDirectoryOps;

typedef struct FileSystem {
	list_t	 list;
	string_t name;

	FileSystemOps *ops;
} FileSystem;

typedef struct FileSystemInfo {
	struct Partition *partition;
	void			 *private_data;
	FileSystemOps	 *ops;
	FsFileOps		  file_ops;
	FsDirectoryOps	  dir_ops;
} FileSystemInfo;

extern list_t fs_list_head;

void register_fs(FileSystem *fs);
void unregister_fs(FileSystem *fs);

FsResult fs_obj_create_file(
	Object *parent, FileSystemInfo *info, string_t name, Object **object,
	ObjectAttr *attr);
FsResult fs_obj_create_dir(
	Object *parent, FileSystemInfo *info, string_t name, Object **object,
	ObjectAttr *attr);

#endif