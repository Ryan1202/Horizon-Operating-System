#include <fs/fs.h>
#include <objects/mount.h>
#include <objects/object.h>

ObjectResult object_mount(Object *origin, Object *dest) {
	if (dest->attr->is_mounted) { return OBJECT_ERROR_INVALID_OPERATION; }
	dest->attr->is_mounted = true;
	dest->origin		   = origin;

	dest->fs_info = origin->fs_info;

	origin->fs_info->ops->fs_mount(origin->fs_info, dest);
	return OBJECT_OK;
}