#ifndef _OBJECT_TYPES_H
#define _OBJECT_TYPES_H

#include "object.h"

ObjectResult init_builtin_types();
Object		*create_object_type(string_t *name);

#endif