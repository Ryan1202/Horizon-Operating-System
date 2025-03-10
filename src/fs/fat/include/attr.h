#ifndef _FAT_ATTR_H
#define _FAT_ATTR_H

#include "dir.h"
#include "fat.h"
#include <fs/fs.h>

FsResult fat_attr_to_sys_attr(
	ShortDir *short_dir, ObjectAttr *attr, FatLocation *location);
FsResult fat_attr_from_sys_attr(
	ShortDir *short_dir, ObjectAttr *attr, FatLocation *location);

#endif