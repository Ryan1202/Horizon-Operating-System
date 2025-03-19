#include <dyn_array.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <objects/ops.h>
#include <objects/types.h>
#include <string.h>
#include <types.h>

const Permission sys_permission = {
	.subject_id = SUBJECT_ID_SYSTEM,
	.permission = {1, 1, 1, 1, 1, 1, 1},
};

Object root_object = {
	.name = STRING_INIT(""), // 根对象的名字不会起到任何作用，所以设为空
	.parent = NULL,

};
Object bus_object = {
	.name = STRING_INIT("Bus"),
};
Object driver_object = {
	.name = STRING_INIT("Driver"),
};
Object device_object = {
	.name = STRING_INIT("Device"),
};
Object volumes_object = {
	.name = STRING_INIT("Volumes"),
};

void init_object_directory(Object *object) {
	list_init(&object->value.directory.children);
}

void init_base_obj_sys_attr(Object *object) {
	object->attr		 = kmalloc_from_template(base_obj_sys_attr);
	object->attr->object = object;
}

/**
 * @brief 初始化对象树
 *
 * @return ObjectResult
 */
ObjectResult init_object_tree() {
	init_object_directory(&root_object);
	init_object_directory(&bus_object);
	init_object_directory(&driver_object);
	init_object_directory(&device_object);
	init_object_directory(&volumes_object);
	init_base_obj_sys_attr(&root_object);
	init_base_obj_sys_attr(&bus_object);
	init_base_obj_sys_attr(&driver_object);
	init_base_obj_sys_attr(&device_object);
	init_base_obj_sys_attr(&volumes_object);
	add_object(&root_object, &driver_object);
	add_object(&root_object, &bus_object);
	add_object(&root_object, &device_object);
	add_object(&root_object, &volumes_object);

	init_builtin_types();
	return OBJECT_OK;
}

ObjectResult object_open_path(Object *parent, Object **child, char *path) {
	Object *object;

	if (*path == '\0') {
		*child = parent;
		return OBJECT_OK;
	}

	string_t name;
	char	 ascii_name[256];
	char	*p = path;
	int		 i = 0;
	name.text  = ascii_name;

	while (*p != '\0' && *p != '\\') {
		ascii_name[i] = *p;
		p++;
		i++;
	}
	bool is_directory = false;
	if (*p == '\\') {
		is_directory = true;
		p++;
	}
	ascii_name[i]	= '\0';
	name.length		= i + 1;
	name.max_length = i + 1;

	ObjectAttr *attr;
	OBJ_RESULT_PASS(obj_lookup(parent, &name, &attr));
	if ((is_directory && attr->type == OBJECT_TYPE_DIRECTORY) ||
		!is_directory) {
		if (attr->type == OBJECT_TYPE_DIRECTORY) {
			if (attr->object != NULL) {
				return object_open_path(attr->object, child, p);
			} else {
				OBJ_RESULT_PASS(obj_open(parent, attr, &name, &object));
				return object_open_path(object, child, p);
			}
		} else {
			return obj_open(parent, attr, &name, child);
		}
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult open_oringinal_object_by_path(char *path, Object **out_object) {
	// 必须从根对象开始
	if (path[0] != '\\') { return OBJECT_ERROR_ILLEGAL_ARGUMENT; }
	path++;

	return object_open_path(&root_object, out_object, path);
}

ObjectResult open_object_by_path(char *path, Object **object) {
	ObjectResult result = open_oringinal_object_by_path(path, object);
	if (result == OBJECT_OK) {
		while ((*object)->attr->type == OBJECT_TYPE_SYM_LINK) {
			*object = (*object)->value.sym_link;
		}
	}
	return result;
}

ObjectResult add_object(Object *parent, Object *child) {
	if (parent->attr->type != OBJECT_TYPE_DIRECTORY) {
		return OBJECT_ERROR_INVALID_OPERATION;
	}

	child->parent = parent;
	list_add_tail(&child->list, &parent->value.directory.children);

	return OBJECT_OK;
}

Object *create_object(Object *parent, string_t name, ObjectAttr attr) {
	Object *object = kmalloc(sizeof(Object));
	if (object == NULL) { return NULL; }

	object->name		 = name;
	object->attr		 = kmalloc_from_template(attr);
	object->attr->object = object;
	object->parent		 = parent;
	object->reference	 = 0;

	ObjectResult result = add_object(parent, object);
	if (result != OBJECT_OK) {
		kfree(object);
		return NULL;
	}

	return object;
}

Object *create_object_directory(
	Object *parent, string_t name, ObjectAttr attr) {
	Object *object	   = create_object(parent, name, attr);
	object->attr->type = OBJECT_TYPE_DIRECTORY;
	if (object == NULL) { return NULL; }

	init_object_directory(object);

	return object;
}

void print_symbol_link(Object *object) {
	if (object != NULL && object != &root_object) {
		print_symbol_link(object->parent);
		printk("\\%s", object->name.text);
	}
}

void print_object_directory(Object *object, int level) {
	Object *child;
	list_for_each_owner (child, &object->value.directory.children, list) {
		for (int j = 0; j < level; j++) {
			printk("|\t");
		}
		printk("|-%s", child->name.text);
		if (child->attr->type == OBJECT_TYPE_SYM_LINK) {
			printk("\t->\t");
			print_symbol_link(child->value.sym_link);
		}
		printk("\n");
		if (child->attr->type == OBJECT_TYPE_DIRECTORY) {
			print_object_directory(child, level + 1);
		}
	}
}

void show_object_tree() {
	Object *object = &root_object;
	printk("root\n_\n");
	print_object_directory(object, 0);
}