#include <dyn_array.h>
#include <objects/object.h>
#include <objects/permission.h>
#include <types.h>

static int type_number = OBJECT_TYPE_BUILTIN_MAX;

#define DEFINE_OBJECT_TYPE(type_name)          \
	{                                          \
		.name		= STRING_INIT(#type_name), \
		.value.type = OBJECT_TYPE_##type_name, \
	}

Object object_builtin_types[OBJECT_TYPE_BUILTIN_MAX] = {
	DEFINE_OBJECT_TYPE(TYPE),	  DEFINE_OBJECT_TYPE(DIRECTORY),
	DEFINE_OBJECT_TYPE(DRIVER),	  DEFINE_OBJECT_TYPE(DEVICE),
	DEFINE_OBJECT_TYPE(FILE),	  DEFINE_OBJECT_TYPE(VALUE),
	DEFINE_OBJECT_TYPE(SYM_LINK), DEFINE_OBJECT_TYPE(PARTITION),
	DEFINE_OBJECT_TYPE(VOLUME),
};

Object object_type_directory = {
	.name = STRING_INIT("ObjectType"),
};

ObjectResult init_builtin_types() {
	init_object_directory(&object_type_directory);
	init_base_obj_sys_attr(&object_type_directory);
	add_object(&root_object, &object_type_directory);

	for (int i = 0; i < OBJECT_TYPE_BUILTIN_MAX; i++) {
		add_object(&object_type_directory, &object_builtin_types[i]);
		init_base_obj_sys_attr(&object_builtin_types[i]);
		object_builtin_types[i].attr->type = OBJECT_TYPE_TYPE;
	}

	return OBJECT_OK;
}

Object *create_object_type(string_t name) {
	Object *object =
		create_object(&object_type_directory, name, base_obj_sys_attr);
	if (object == NULL) { return NULL; }

	object->value.type = type_number - 1;

	return object;
}