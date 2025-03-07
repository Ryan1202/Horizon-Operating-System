#include "kernel/list.h"
#include "kernel/memory.h"
#include "objects/transfer.h"
#include <fs/fs.h>
#include <multiple_return.h>
#include <objects/object.h>
#include <objects/ops.h>

ObjectResult obj_search(
	Object *parent, DEF_MRET(Object *, child), string_t name, ObjectType type) {
	Object *child;
	list_for_each_owner (child, &parent->value.directory.children, list) {
		if (child->name.length == name.length &&
			strncmp(child->name.text, name.text, name.length) == 0) {
			if (child->type == type) {
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
	ObjectResult result = obj_search(parent, &child, name, OBJECT_TYPE_FILE);
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
	ObjectResult result =
		obj_search(parent, &child, name, OBJECT_TYPE_DIRECTORY);
	if (result == OBJECT_OK) {
		MRET(child) = child;
		child->reference++;
		return OBJECT_OK;
	}
	// 如果缓存中找不到，则调用文件系统接口读取
	else if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_opendir(
			parent->fs_info, parent->value.directory.data, name, &MRET(child));
		child->reference++;
		if (result == FS_OK) return OBJECT_OK;
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_close(Object *object) {
	object->reference--;
	if (object->reference > 0) return OBJECT_OK;
	if (!object->is_mounted && !object->fixed) {
		if (object->release_data != NULL) object->release_data(object);
		if (object->fs_info != NULL) {
			if (object->type == OBJECT_TYPE_FILE) {
				object->fs_info->file_ops.fs_close(object);
			} else if (object->type == OBJECT_TYPE_DIRECTORY) {
				object->fs_info->dir_ops.fs_closedir(object);
			}
		}
		list_del(&object->list);
		obj_close(object->parent);
		kfree(object);
	}
	return OBJECT_OK;
}

ObjectResult obj_delete_file(Object *parent, string_t name) {
	Object		*child;
	ObjectResult result = obj_search(parent, &child, name, OBJECT_TYPE_FILE);
	if (result == OBJECT_OK) {
		if (child->reference > 0) return OBJECT_ERROR_OCCUPIED;
		result = obj_close(child);
	}
	if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_delete_file(parent, name);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}

	return OBJECT_OK;
}
