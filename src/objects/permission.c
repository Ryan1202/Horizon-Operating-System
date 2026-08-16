#include <kernel/list.h>
#include <objects/object.h>
#include <objects/permission.h>

Permission *get_permission_info(ObjectAttr *attr) {
	// 当前阶段只有系统主体；用户身份将在进程层接入。
	size_t subject_id = SUBJECT_ID_SYSTEM;
	if (subject_id == SUBJECT_ID_SYSTEM) {
		return &attr->system_permission;
	} else if (subject_id == attr->owner_id) {
		return &attr->owner_permission;
	} else {
		Permission *permission;
		list_for_each_owner (permission, &attr->permission_lh, list) {
			if (permission->subject_id == subject_id) return permission;
		}
	}
	return NULL;
}
