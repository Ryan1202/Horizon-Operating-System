#include <fs/fs.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <multiple_return.h>
#include <objects/object.h>
#include <objects/ops.h>
#include <objects/permission.h>
#include <objects/transfer.h>
#include <string.h>

ObjectResult obj_lookup_cache(
	Object *parent, DEF_MRET(Object *, child), string_t *name) {
	Object *child;
	list_for_each_owner (child, &parent->value.directory.children, list) {
		if (child->name.length == name->length &&
			strncmp(child->name.text, name->text, name->length) == 0) {
			MRET(child) = child;
			return OBJECT_OK;
		}
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_lookup(
	Object *parent, string_t *name, DEF_MRET(ObjectAttr *, attr)) {
	Object		*child;
	ObjectResult result = obj_lookup_cache(parent, &child, name);
	if (result == OBJECT_OK) {
		MRET(attr) = child->attr;
		return OBJECT_OK;
	}
	if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_lookup(
			parent->fs_info, parent, name, &MRET(attr));
		if (result == FS_OK) return OBJECT_OK;
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_open(
	Object *parent, ObjectAttr *attr, string_t *name,
	DEF_MRET(Object *, child)) {
	Object		*child;
	ObjectResult result = obj_lookup_cache(parent, &child, name);
	if (result == OBJECT_OK) {
		MRET(child) = child;
		child->reference++;
		return OBJECT_OK;
	}
	// 如果缓存中找不到，则调用文件系统接口读取
	else if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->file_ops.fs_open(
			parent->fs_info, parent, attr, name, &MRET(child));
		MRET(child)->reference++;
		if (result == FS_OK) return OBJECT_OK;
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult obj_opendir(Object *parent, DEF_MRET(ObjectIterator *, iter)) {
	ObjectIterator *iter = kmalloc(sizeof(ObjectIterator));
	if (parent->fs_info != NULL) {
		iter->type		= ITERATOR_TYPE_FS;
		FsResult result = parent->fs_info->dir_ops.fs_opendir(
			parent->fs_info, parent, &iter->fs_iterator);
		if (result != FS_OK) return OBJECT_ERROR_CANNOT_FIND;
	} else {
		iter->type		   = ITERATOR_TYPE_MEM;
		iter->current_node = parent->value.directory.children.next;
	}
	MRET(iter) = iter;

	return OBJECT_OK;
}

ObjectResult obj_readdir(ObjectIterator *iterator, DEF_MRET(Object *, object)) {
	if (iterator->type == ITERATOR_TYPE_FS) {
		FsResult result = iterator->parent_object->fs_info->dir_ops.fs_readdir(
			iterator->parent_object->value.directory.data, iterator,
			&MRET(object));
		if (result == FS_ERROR_CANNOT_FIND) return OBJECT_ERROR_CANNOT_FIND;
		else if (result != FS_OK) return OBJECT_ERROR_CANNOT_FIND;
	} else {
		if (iterator->current_node ==
			&iterator->parent_object->value.directory.children) {
			return OBJECT_ERROR_CANNOT_FIND;
		}

		MRET(object) = list_owner(iterator->current_node, Object, list);
		iterator->current_node = iterator->current_node->next;
	}
	return OBJECT_OK;
}

ObjectResult obj_closedir(ObjectIterator *iterator) {
	if (iterator->type == ITERATOR_TYPE_FS) {
		if (iterator->fs_iterator != NULL) {
			FsResult result =
				iterator->parent_object->fs_info->dir_ops.fs_closedir(iterator);
			if (result != FS_OK) return OBJECT_ERROR_OTHER;
		}
	}
	kfree(iterator);
	return OBJECT_OK;
}

ObjectResult obj_close(Object *object) {
	object->reference--;
	if (object->reference > 0) return OBJECT_OK;
	if (object->fs_info == NULL) return OBJECT_OK;
	if (!object->attr->is_mounted) {
		if (object->release_data != NULL) object->release_data(object);
		if (object->attr->type == OBJECT_TYPE_FILE) {
			object->fs_info->file_ops.fs_close(object);
		} else if (object->attr->type == OBJECT_TYPE_DIRECTORY) {
			object->fs_info->dir_ops.fs_closedir(
				object->value.directory.fs_iterator);
		}
		list_del(&object->list);
		obj_close(object->parent);
		kfree(object);
	}
	return OBJECT_OK;
}

ObjectResult obj_create_file(Object *parent, string_t *name) {
	Object		*child;
	ObjectResult result = obj_lookup_cache(parent, &child, name);
	if (result == OBJECT_OK) return OBJECT_ERROR_ALREADY_EXISTS;
	if (parent->fs_info != NULL) {
		FsResult result =
			parent->fs_info->dir_ops.fs_create_file(parent, name, &child);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}
	return OBJECT_OK;
}

ObjectResult obj_delete_file(Object *parent, ObjectAttr *attr, string_t *name) {
	// 先打开文件检查权限，再决定要不要删除
	Permission *permission = get_permission_info(attr);
	if (!permission->permission.delete) return OBJECT_ERROR_NO_PERMISSION;

	if (attr->object != NULL) {
		Object *child = attr->object;
		if (child->reference > 0) return OBJECT_ERROR_OCCUPIED;
	}

	if (parent->fs_info != NULL) {
		FsResult result =
			parent->fs_info->dir_ops.fs_delete_file(parent, attr, name);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}

	return OBJECT_OK;
}

ObjectResult obj_mkdir(Object *parent, string_t *name) {
	Object		*child;
	ObjectResult result = obj_lookup_cache(parent, &child, name);
	if (result == OBJECT_OK) return OBJECT_ERROR_ALREADY_EXISTS;
	if (parent->fs_info != NULL) {
		FsResult result =
			parent->fs_info->dir_ops.fs_mkdir(parent, name, &child);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}
	return OBJECT_OK;
}

ObjectResult obj_rmdir(Object *parent, ObjectAttr *attr, string_t *name) {
	// 先打开检查权限，再决定要不要删除
	Permission *permission = get_permission_info(attr);
	if (!permission->permission.delete) return OBJECT_ERROR_NO_PERMISSION;

	if (attr->object != NULL) {
		Object *child = attr->object;
		if (child->reference > 0) return OBJECT_ERROR_OCCUPIED;
		if (!child->attr->is_mounted) return OBJECT_ERROR_OCCUPIED;
		if (!list_empty(&child->value.directory.children))
			return OBJECT_ERROR_NOT_EMPTY;
	}

	if (parent->fs_info != NULL) {
		FsResult result = parent->fs_info->dir_ops.fs_rmdir(parent, attr, name);
		if (result == FS_OK) return OBJECT_OK;
		else if (result == FS_ERROR_NOT_EMPTY) return OBJECT_ERROR_NOT_EMPTY;
		else return OBJECT_ERROR_OTHER;
	}
	return OBJECT_OK;
}

ObjectResult obj_get_attr(Object *object, ObjectAttr *attr) {
	*attr = *object->attr;
	return OBJECT_OK;
}

ObjectResult obj_set_attr(Object *object, ObjectAttr *attr) {
	Permission *permission = get_permission_info(object->attr);
	if (!permission->permission.set_attr) return OBJECT_ERROR_NO_PERMISSION;

	if (object->fs_info != NULL) {
		FsResult result = object->fs_info->dir_ops.fs_set_attr(object, attr);
		if (result != FS_OK) return OBJECT_ERROR_OTHER;
	}
	*object->attr = *attr;
	return OBJECT_OK;
}
