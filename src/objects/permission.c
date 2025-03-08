#include <kernel/list.h>
#include <kernel/thread.h>
#include <objects/object.h>
#include <objects/permission.h>
#include <stdint.h>

Permission *get_permission_info(Object *object) {
	size_t subject_id = get_current_subject_id();
	if (subject_id == SUBJECT_ID_SYSTEM) {
		return &object->attr.system_permission;
	} else if (subject_id == object->attr.owner_id) {
		return &object->attr.owner_permission;
	} else {
		Permission *permission;
		list_for_each_owner (permission, &object->attr.permission_lh, list) {
			if (permission->subject_id == subject_id) return permission;
		}
	}
	return NULL;
}