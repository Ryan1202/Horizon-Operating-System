#ifndef _OBJECT_HANDLE_H
#define _OBJECT_HANDLE_H

#include "object.h"

typedef struct ObjectHandle {
	Object *object;
	void   *handle_data;
	void   *buf;
} ObjectHandle;

ObjectHandle *object_handle_create(Object *object);
ObjectResult  object_handle_delete(ObjectHandle *handle);

#endif