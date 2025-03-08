#include "fs/fs.h"
#include "include/dir.h"
#include <objects/object.h>
#include <objects/permission.h>
#include <stdint.h>
#include <string.h>

FsResult fat_attr_to_sys_attr(ShortDir *short_dir, ObjectAttr *attr) {
	uint8_t fat_attr = short_dir->attr;
	if (fat_attr & ATTR_ARCHIVE) {
		attr->size			 = short_dir->file_size;
		attr->is_mounted	 = 0;
		Permission *all_user = &attr->all_user_permission;
		Permission *system	 = &attr->system_permission;
		Permission *owner	 = &attr->owner_permission;
		Permission *admin	 = &attr->admin_permission;
		memset(&all_user->permission, 0xff, sizeof(all_user->permission));
		memset(&system->permission, 0xff, sizeof(system->permission));
		memset(&owner->permission, 0xff, sizeof(owner->permission));
		memset(&admin->permission, 0xff, sizeof(admin->permission));
		all_user->subject_id = SUBJECT_ID_ALL;
		system->subject_id	 = SUBJECT_ID_SYSTEM;
		admin->subject_id	 = SUBJECT_ID_ADMIN;

		if (fat_attr & ATTR_SYSTEM) {
			all_user->permission.read	 = 0;
			all_user->permission.write	 = 0;
			all_user->permission.execute = 0;
			all_user->permission.delete	 = 0;
			all_user->permission.rename	 = 0;
			admin->permission.delete	 = 0;
			admin->permission.rename	 = 0;
			admin->permission.write		 = 0;
			owner->subject_id			 = SUBJECT_ID_SYSTEM;
			attr->owner_id				 = SUBJECT_ID_SYSTEM;
		} else {
			owner->subject_id = SUBJECT_ID_ALL;
			attr->owner_id	  = SUBJECT_ID_ALL;
		}
		if (fat_attr & ATTR_DIRECTORY) {
			attr->type = OBJECT_TYPE_DIRECTORY;
			if (fat_attr & ATTR_SYSTEM) { all_user->permission.execute = 1; }
		}

		if (fat_attr & ATTR_READ_ONLY) {
			attr->all_user_permission.permission.write = 0;
		}
		if (fat_attr & ATTR_HIDDEN) {
			attr->all_user_permission.permission.visible = 0;
		}
	}
	return FS_OK;
}

FsResult fat_attr_from_sys_attr(ShortDir *short_dir, ObjectAttr *attr) {
	Permission *all_user = &attr->all_user_permission;
	Permission *owner	 = &attr->owner_permission;

	short_dir->attr = ATTR_ARCHIVE;
	if (all_user->permission.write == 0 && all_user->permission.read == 1) {
		short_dir->attr |= ATTR_READ_ONLY;
	}
	if (all_user->permission.visible == 0) { short_dir->attr |= ATTR_HIDDEN; }
	if (attr->type == OBJECT_TYPE_DIRECTORY) {
		short_dir->attr |= ATTR_DIRECTORY;
	} else {
		short_dir->file_size = attr->size;
	}
	if (owner->subject_id == SUBJECT_ID_SYSTEM) {
		short_dir->attr |= ATTR_SYSTEM;
	}

	return FS_OK;
}
