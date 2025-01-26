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
	init_object_directory(&driver_object, OBJECT_DIR_SIZE_LARGE);
	init_object_directory(&device_object, OBJECT_DIR_SIZE_LARGE);
	add_object(&root_object, &driver_object);
	add_object(&root_object, &device_object);

	init_builtin_types();
	return OBJECT_OK;
}

ObjectResult add_object(Object *parent, Object *child) {
	if (parent->type != OBJECT_TYPE_DIRECTORY) {
		return OBJECT_ERROR_INVALID_OPERATION;
	}

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

void print_object_directory(Object *object, int level) {
	for (int i = 0; i < object->value.directory.children->size; i++) {
		Object *child =
			dyn_array_get(object->value.directory.children, Object *, i);
		for (int j = 0; j < level; j++) {
			printk("|\t");
		}
		printk("|-%s\n", child->name.text);
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