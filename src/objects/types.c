#include "kernel/list.h"
#include <dyn_array.h>
#include <objects/object.h>
#include <types.h>

static int type_number = OBJECT_TYPE_BUILTIN_MAX;

#define DEFINE_OBJECT_TYPE(type_name)                              \
	{                                                              \
		.name = STRING_INIT(#type_name), .type = OBJECT_TYPE_TYPE, \
		.value.type = OBJECT_TYPE_##type_name                      \
	}

Object object_builtin_types[OBJECT_TYPE_BUILTIN_MAX] = {
	DEFINE_OBJECT_TYPE(TYPE),	  DEFINE_OBJECT_TYPE(DIRECTORY),
	DEFINE_OBJECT_TYPE(DRIVER),	  DEFINE_OBJECT_TYPE(DEVICE),
	DEFINE_OBJECT_TYPE(FILE),	  DEFINE_OBJECT_TYPE(VALUE),
	DEFINE_OBJECT_TYPE(SYM_LINK), DEFINE_OBJECT_TYPE(PARTITION),
};

Object object_type_directory = {
	.name = STRING_INIT("ObjectType"),
	.type = OBJECT_TYPE_DIRECTORY,
};

ObjectResult init_builtin_types() {
	list_init(&object_type_directory.value.directory.children);
	add_object(&root_object, &object_type_directory);

	for (int i = 0; i < OBJECT_TYPE_BUILTIN_MAX; i++) {
		add_object(&object_type_directory, &object_builtin_types[i]);
	}

	return OBJECT_OK;
}

Object *create_object_type(string_t name) {
	Object *object =
		create_object(&object_type_directory, name, OBJECT_TYPE_TYPE);
	if (object == NULL) { return NULL; }

	object->value.type = type_number - 1;

	return object;
}