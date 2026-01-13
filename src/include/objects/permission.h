#ifndef _OBJECT_PERMISSION_H
#define _OBJECT_PERMISSION_H

#include <kernel/list.h>
#include <stdint.h>

#define SUBJECT_ID_ALL	  0
#define SUBJECT_ID_SYSTEM 1
#define SUBJECT_ID_ADMIN  2

typedef struct Permission {
	list_t	 list;
	uint32_t subject_id;
	struct {
		uint32_t visible : 1;
		uint32_t read	 : 1;
		uint32_t write	 : 1;
		uint32_t execute : 1; // 对于文件表示执行，对于目录表示访问
		uint32_t delete	  : 1;
		uint32_t rename	  : 1;
		uint32_t set_attr : 1;
	} permission;
} Permission;

struct Object;
struct ObjectAttr;
Permission *get_permission_info(struct ObjectAttr *attr);

#endif