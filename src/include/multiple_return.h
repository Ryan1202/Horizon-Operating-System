#ifndef _MULTIPLE_RETURN_H_
#define _MULTIPLE_RETURN_H_

#define DEF_MRET(type, name) type *out_##name
#define MRET(name)			 (*out_##name)

#endif