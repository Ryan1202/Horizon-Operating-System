#include <fs/fs.h>
#include <kernel/memory.h>
#include <objects/handle.h>
#include <objects/object.h>

ObjectHandle *object_handle_create(Object *object) {
	ObjectHandle *handle = kmalloc(sizeof(ObjectHandle));
	if (handle == NULL) return NULL;
	handle->object		= object;
	handle->buf			= NULL;
	handle->handle_data = NULL;
	object->fs_info->file_ops.fs_create_handle(handle);
	return handle;
}

ObjectResult object_handle_delete(ObjectHandle *handle) {
	handle->object->fs_info->file_ops.fs_delete_handle(handle);
	kfree(handle);
	return OBJECT_OK;
}