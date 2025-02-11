#include <dyn_array.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <objects/types.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

Object root_object = {
	.name = STRING_INIT(""), // 根对象的名字不会起到任何作用，所以设为空
	.type	= OBJECT_TYPE_DIRECTORY,
	.parent = NULL,
};
Object bus_object = {
	.name = STRING_INIT("Bus"),
	.type = OBJECT_TYPE_DIRECTORY,
};
Object driver_object = {
	.name = STRING_INIT("Driver"),
	.type = OBJECT_TYPE_DIRECTORY,
};
Object device_object = {
	.name = STRING_INIT("Device"),
	.type = OBJECT_TYPE_DIRECTORY,
};

/**
 * @brief 初始化对象目录的children结构
 *
 * @param object
 * @param block_size
 * @return ObjectResult
 */
ObjectResult init_object_directory(Object *object, size_t block_size) {
	DynArray *children = dyn_array_new(sizeof(Object *), block_size);
	if (children == NULL) { return OBJECT_ERROR_MEMORY; }

	object->value.directory.children = children;
	return OBJECT_OK;
}

/**
 * @brief 初始化对象树
 *
 * @return ObjectResult
 */
ObjectResult init_object_tree() {
	init_object_directory(&root_object, OBJECT_DIR_SIZE_SMALL);
	init_object_directory(&bus_object, OBJECT_DIR_SIZE_SMALL);
	init_object_directory(&driver_object, OBJECT_DIR_SIZE_LARGE);
	init_object_directory(&device_object, OBJECT_DIR_SIZE_LARGE);
	add_object(&root_object, &driver_object);
	add_object(&root_object, &bus_object);
	add_object(&root_object, &device_object);

	init_builtin_types();
	return OBJECT_OK;
}

ObjectResult find_object_by_name(
	Object *parent, Object **out_child, string_t *name) {
	Object *child;
	dyn_array_foreach(parent->value.directory.children, Object *, child) {
		if (child->name.length == name->length &&
			strncmp(child->name.text, name->text, name->length) == 0) {
			*out_child = child;
			return OBJECT_OK;
		}
	}
	return OBJECT_ERROR_CANNOT_FIND;
}

ObjectResult open_oringinal_object_by_ascii_path(
	char *path, Object **out_object) {
	// 必须从根对象开始
	if (path[0] != '\\') { return OBJECT_ERROR_ILLEGAL_ARGUMENT; }
	path++;

	char	 ascii_name[256];
	string_t name	= {0, 0, ascii_name};
	Object	*object = &root_object;
	while (*path) {
		int i = 0;
		while (*path != '\0' && *path != '\\') {
			ascii_name[i] = *path;
			path++;
			i++;
		}
		if (*path == '\\') { path++; }
		ascii_name[i]	= '\0';
		name.length		= i + 1;
		name.max_length = i + 1;

		Object		*child;
		ObjectResult result = find_object_by_name(object, &child, &name);
		if (result != OBJECT_OK) { return result; }

		object = child;
	}
	*out_object = object;
	return OBJECT_OK;
}

ObjectResult open_object_by_ascii_path(char *path, Object **object) {
	ObjectResult result = open_oringinal_object_by_ascii_path(path, object);
	if (result == OBJECT_OK) {
		while ((*object)->type == OBJECT_TYPE_SYM_LINK) {
			*object = (*object)->value.sym_link;
		}
	}
	return result;
}

ObjectResult add_object(Object *parent, Object *child) {
	if (parent->type != OBJECT_TYPE_DIRECTORY) {
		return OBJECT_ERROR_INVALID_OPERATION;
	}

	child->parent = parent;
	append_object(parent, child);

	return OBJECT_OK;
}

Object *create_object(Object *parent, string_t *name, ObjectType type) {
	Object *object = kmalloc(sizeof(Object));
	if (object == NULL) { return NULL; }

	object->name   = *name;
	object->type   = type;
	object->parent = parent;

	add_object(parent, object);

	return object;
}

Object *create_object_directory(Object *parent, string_t *name) {
	Object *object = create_object(parent, name, OBJECT_TYPE_DIRECTORY);
	if (object == NULL) { return NULL; }

	init_object_directory(object, OBJECT_DIR_SIZE_SMALL);

	return object;
}

void print_symbol_link(Object *object) {
	if (object != NULL && object != &root_object) {
		print_symbol_link(object->parent);
		printk("\\%s", object->name.text);
	}
}

void print_object_directory(Object *object, int level) {
	for (int i = 0; i < object->value.directory.children->size; i++) {
		Object *child =
			dyn_array_get(object->value.directory.children, Object *, i);
		for (int j = 0; j < level; j++) {
			printk("|\t");
		}
		printk("|-%s", child->name.text);
		if (child->type == OBJECT_TYPE_SYM_LINK) {
			printk("\t->\t");
			print_symbol_link(child->value.sym_link);
		}
		printk("\n");
		if (child->type == OBJECT_TYPE_DIRECTORY) {
			print_object_directory(child, level + 1);
		}
	}
}

void show_object_tree() {
	Object *object = &root_object;
	printk("root\n_\n");
	print_object_directory(object, 0);
}