#include "kernel/list.h"
#include "kernel/memory.h"
#include "objects/permission.h"
#include "objects/transfer.h"
#include "string.h"
#include <fs/fs.h>
#include <multiple_return.h>
#include <objects/object.h>
#include <objects/ops.h>

ObjectResult obj_search(
	Object *parent, DEF_MRET(Object *, child), string_t name,
	bool is_directory) {
	Object *child;
	list_for_each_owner (child, &parent->value.directory.children, list) {
		if (child->name.length == name.length &&
			strncmp(child->name.text, name.text, name.length) == 0) {
			if ((is_directory && child->attr.type == OBJECT_TYPE_DIRECTORY) ||
				(!is_directory && child->attr.type != OBJECT_TYPE_DIRECTORY)) {
				MRET(child) = child;
				return OBJECT_OK;
			}
		}
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_open(
	Object *parent, DEF_MRET(Object *, child), string_t name) {
	Object		*child;
	ObjectResult result = obj_search(parent, &child, name, false);
	if (result == OBJECT_OK) {
		MRET(child) = child;
		child->reference++;
		return OBJECT_OK;
	}
	// 如果缓存中找不到，则调用文件系统接口读取
	else if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->file_ops.fs_open(
			parent->fs_info, parent->value.directory.data, name, &MRET(child));
		child->reference++;
		if (result == FS_OK) return OBJECT_OK;
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_opendir(
	Object *parent, DEF_MRET(Object *, child), string_t name) {
	Object		*child;
	ObjectResult result = obj_search(parent, &child, name, true);
	if (result == OBJECT_OK) {
		Permission *permission = get_permission_info(child);
		if (!permission->permission.execute) return OBJECT_ERROR_NO_PERMISSION;
		MRET(child) = child;
		child->reference++;
		return OBJECT_OK;
	}
	// 如果缓存中找不到，则调用文件系统接口读取
	else if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_opendir(
			parent->fs_info, parent->value.directory.data, name, &MRET(child));
		if (result != FS_OK) return OBJECT_ERROR_CANNOT_FIND;
		ObjectAttr attr;
		obj_get_attr(child, &attr);
		Permission *permission = get_permission_info(child);
		if (!permission->permission.execute) return OBJECT_ERROR_NO_PERMISSION;
		child->reference++;
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_close(Object *object) {
	object->reference--;
	if (object->reference > 0) return OBJECT_OK;
	if (object->fs_info == NULL) return OBJECT_OK;
	if (!object->attr.is_mounted) {
		if (object->release_data != NULL) object->release_data(object);
		if (object->attr.type == OBJECT_TYPE_FILE) {
			object->fs_info->file_ops.fs_close(object);
		} else if (object->attr.type == OBJECT_TYPE_DIRECTORY) {
			object->fs_info->dir_ops.fs_closedir(object);
		}
		list_del(&object->list);
		obj_close(object->parent);
		kfree(object);
	}
	return OBJECT_OK;
}

ObjectResult obj_create_file(Object *parent, string_t name) {
	Object		*child;
	ObjectResult result = obj_search(parent, &child, name, false);
	if (result == OBJECT_OK) return OBJECT_ERROR_ALREADY_EXISTS;
	if (parent->fs_info != NULL) {
		FsResult result =
			parent->fs_info->dir_ops.fs_create_file(parent, name, &child);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}
	return OBJECT_OK;
}

ObjectResult obj_delete_file(Object *parent, string_t name) {
	Object *child;
	// 先打开文件检查权限，再决定要不要删除
	OBJ_RESULT_PASS(obj_open(parent, &child, name));

	if (child->reference > 0) return OBJECT_ERROR_OCCUPIED;
	Permission *permission = get_permission_info(child);
	if (!permission->permission.delete) return OBJECT_ERROR_NO_PERMISSION;
	OBJ_RESULT_PASS(obj_close(child));

	if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_delete_file(parent, name);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}

	return OBJECT_OK;
}

ObjectResult obj_mkdir(Object *parent, string_t name) {
	Object		*child;
	ObjectResult result = obj_search(parent, &child, name, true);
	if (result == OBJECT_OK) return OBJECT_ERROR_ALREADY_EXISTS;
	if (parent->fs_info != NULL) {
		FsResult result =
			parent->fs_info->dir_ops.fs_mkdir(parent, name, &child);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}
	return OBJECT_OK;
}

ObjectResult obj_rmdir(Object *parent, string_t name) {
	Object *child;
	// 先打开检查权限，再决定要不要删除
	OBJ_RESULT_PASS(obj_open(parent, &child, name));

	if (child->reference > 0) return OBJECT_ERROR_OCCUPIED;
	Permission *permission = get_permission_info(child);
	if (!permission->permission.delete) return OBJECT_ERROR_NO_PERMISSION;
	if (!child->attr.is_mounted) return OBJECT_ERROR_OCCUPIED;
	if (!list_empty(&child->value.directory.children))
		return OBJECT_ERROR_NOT_EMPTY;

	OBJ_RESULT_PASS(obj_close(child));

	if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_rmdir(parent, name);
		if (result == FS_OK) return OBJECT_OK;
		else if (result == FS_ERROR_NOT_EMPTY) return OBJECT_ERROR_NOT_EMPTY;
		else return OBJECT_ERROR_OTHER;
	}
	return OBJECT_OK;
}

ObjectResult obj_get_attr(Object *object, ObjectAttr *attr) {
	// 在打开object时会自动生成attr,直接复制即可
	*attr = object->attr;
	return OBJECT_OK;
}

ObjectResult obj_set_attr(Object *object, ObjectAttr *attr) {
	Permission *permission = get_permission_info(object);
	if (!permission->permission.set_attr) return OBJECT_ERROR_NO_PERMISSION;

	if (object->fs_info != NULL) {
		FsResult result = object->fs_info->dir_ops.fs_set_attr(object, attr);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}
	object->attr = *attr;
	return OBJECT_OK;
}
