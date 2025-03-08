#include "objects/transfer.h"
#include "stdint.h"
#include <const.h>
#include <fs/fs.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <objects/ops.h>

LIST_HEAD(fs_list_head);

void register_fs(FileSystem *fs) {
	list_add_tail(&fs->list, &fs_list_head);
}

void unregister_fs(FileSystem *fs) {
	list_del(&fs->list);
}

// 为文件系统实现对象树的接口
TransferResult fs_obj_read(
	struct Object *object, TransferDirection direction, uint8_t *buf,
	size_t size) {
	FsResult result = object->fs_info->file_ops.fs_read(object, buf, size);
	if (result == FS_OK) return TRANSFER_OK;
	else return TRANSFER_ERROR_OTHER;
}

TransferResult fs_obj_write(
	struct Object *object, TransferDirection direction, uint8_t *buf,
	size_t size) {
	FsResult result = object->fs_info->file_ops.fs_write(object, buf, size);
	if (result == FS_OK) return TRANSFER_OK;
	else return TRANSFER_ERROR_OTHER;
}

FsResult fs_obj_create_file(
	Object *parent, FileSystemInfo *info, string_t name, Object **object,
	ObjectAttr *attr) {
	*object		= create_object(parent, name, *attr);
	Object *out = *object;
	if (out == NULL) return FS_ERROR_OTHER;

	out->in.type	= TRANSFER_TYPE_STREAM;
	out->in.stream	= fs_obj_read;
	out->out.type	= TRANSFER_TYPE_STREAM;
	out->out.stream = fs_obj_write;

	out->fs_info = info;

	return FS_OK;
}

FsResult fs_obj_create_dir(
	Object *parent, FileSystemInfo *info, string_t name, Object **object,
	ObjectAttr *attr) {
	*object		= create_object_directory(parent, name, *attr);
	Object *out = *object;
	if (out == NULL) return FS_ERROR_OTHER;

	out->fs_info = info;

	return FS_OK;
}
