#ifndef _OBJECT_OPS_H
#define _OBJECT_OPS_H

#include "object.h"
#include <multiple_return.h>

#define OBJ_READ_STREAM(object)		(object)->in.stream
#define OBJ_WRITE_STREAM(object)	(object)->out.stream
#define OBJ_READ_BLOCK(object)		(object)->in.block
#define OBJ_WRITE_BLOCK(object)		(object)->out.block
#define OBJ_READ_INTERRUPT(object)	(object)->in.interrupt
#define OBJ_WRITE_INTERRUPT(object) (object)->out.interrupt

ObjectResult obj_open(Object *parent, DEF_MRET(Object *, child), string_t name);
ObjectResult obj_opendir(
	Object *parent, DEF_MRET(Object *, child), string_t name);
ObjectResult obj_close(Object *object);
ObjectResult obj_create_file(Object *parent, string_t name);
ObjectResult obj_delete_file(Object *parent, string_t name);
ObjectResult obj_mkdir(Object *parent, string_t name);
ObjectResult obj_rmdir(Object *parent, string_t name);

#endif